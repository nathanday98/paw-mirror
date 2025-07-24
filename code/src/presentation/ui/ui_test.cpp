#include "ui_test.h"

#include <core/memory_types.h>
#include <core/std.h>
#include <core/assert.h>
#include <core/math.h>
#include <core/reflection.h>
#include <core/slice.inl>
#include <core/arena.h>
#include <core/memory.inl>
#include <core/src_location_types.h>

PAW_DISABLE_ALL_WARNINGS_BEGIN
#include <cstdio>
#include <type_traits>
#include <stdio.h>
PAW_DISABLE_ALL_WARNINGS_END

#include "widget.h"

static Float4 rgb(Byte r, Byte g, Byte b)
{
	static constexpr F32 d = 1.0f / 255.0f;
	return {static_cast<F32>(r) * d, static_cast<F32>(g) * d, static_cast<F32>(b) * d, 1.0f};
}

static Float4 argb(Byte a, Byte r, Byte g, Byte b)
{
	static constexpr F32 d = 1.0f / 255.0f;
	return {static_cast<F32>(r) * d, static_cast<F32>(g) * d, static_cast<F32>(b) * d, static_cast<F32>(a) * d};
}

static ArenaAllocator* g_widget_allocator = nullptr;
static Widget* g_current_parent = nullptr;
static Widget* g_current_sibling = nullptr;

class CursorInteract;

struct CursorInteractBox
{
	CursorInteract* widget = nullptr;
	Float2 position{};
	Float2 size{};
};

class WidgetContext : NonCopyable
{
public:
	WidgetContext()
	{
	}

	// #TODO: Move this back into the constructor when it's no longer a global
	void init(IAllocator* allocator)
	{
		render_items = PAW_NEW_SLICE_IN(allocator, 4096, RenderItem);
		cursor_interact_boxes = PAW_NEW_SLICE_IN(allocator, 512, CursorInteractBox);
	}

	void frame_reset(Float2 root_clip_size)
	{
		current_clip_rect = {Float2{}, root_clip_size};
		render_item_index = 1;
		cursor_interact_box_index = 0;
	}

	void reset_mouse_move()
	{
		current_frame_hovered_element_owner = nullptr;
	}

	Widget* get_current_frame_hovered() const
	{
		return current_frame_hovered_element_owner;
	}

	void push_hovered(Widget* interact)
	{
		current_frame_hovered_element_owner = interact;
	}

	void push_render_box(RenderBox&& box)
	{
		RenderItem& item = render_items[render_item_index++];
		item.type = RenderItem::Type::Box;
		item.box = box;
		item.clip_rect = current_clip_rect;
	}

	void push_render_line(RenderLine&& line)
	{
		RenderItem& item = render_items[render_item_index++];
		item.type = RenderItem::Type::Line;
		item.line = line;
		item.clip_rect = current_clip_rect;
	}

	RenderClipRect const push_clip_rect(RenderClipRect clip_rect)
	{
		RenderClipRect const previous_clip_rect = current_clip_rect;
		current_clip_rect = clip_rect;
		return previous_clip_rect;
	}

	// void render(Gdiplus::Graphics& graphics)
	// {
	// 	for (S32 i = 0; i < render_item_index; i++)
	// 	{
	// 		RenderItem const& render_item = render_items[i];
	// 		switch (render_item.type)
	// 		{
	// 			case RenderItem::Type::Box:
	// 			{
	// 				RenderBox const& render_box = render_item.box;
	// 				Gdiplus::SolidBrush brush(render_box.color);
	// 				graphics.FillRectangle(&brush, render_box.position.x, render_box.position.y, render_box.size.x, render_box.size.y);
	// 			}
	// 			break;

	// 			case RenderItem::Type::ClipRect:
	// 			{
	// 				RenderClipRect const& clip_rect = render_item.clip_rect;
	// 				graphics.SetClip(Gdiplus::RectF{clip_rect.position.x, clip_rect.position.y, clip_rect.size.x, clip_rect.size.y});
	// 			}
	// 			break;

	// 			case RenderItem::Type::Line:
	// 			{
	// 				RenderLine const& render_line = render_item.line;
	// 				Gdiplus::Pen pen(render_line.color, render_line.thickness);
	// 				graphics.DrawLine(&pen, render_line.start.x, render_line.start.y, render_line.end.x, render_line.end.y);
	// 			}
	// 			break;
	// 		}
	// 	}
	// }

	Slice<RenderItem const> GetRenderItems()
	{
		return {render_items.items, render_item_index};
	}

private:
	Slice<RenderItem> render_items{nullptr, 0};
	Slice<CursorInteractBox> cursor_interact_boxes{nullptr, 0};
	Widget* current_frame_hovered_element_owner = nullptr;
	S32 render_item_index = 0;
	S32 cursor_interact_box_index = 0;
	RenderClipRect current_clip_rect{};
};

static Widget* g_last_frame_hovered_element_owner = nullptr;
static WidgetContext g_background_widget_context{};
static WidgetContext g_foreground_widget_context{};
static WidgetContext* g_widget_context = nullptr;
static Widget* g_active_element_owner = nullptr;
static Widget* g_just_clicked_element_owner = nullptr;
static PtrSize g_current_frame_index = 0;
static PtrSize g_new_widget_count = 0;

struct WidgetBuildContext;
class ElementTree;
static ElementTree* g_element_tree = nullptr;

static PtrSize g_current_widget_id = g_null_widget_id;

struct WidgetTempID
{
	PtrSize const previous_id;
};

static WidgetTempID begin_widget_id(PtrSize id)
{
	WidgetTempID result{
		.previous_id = g_current_widget_id,
	};

	g_current_widget_id = id;

	return result;
}

static void end_widget_id(WidgetTempID const& /* temp */)
{
	// g_current_widget_id = temp.previous_id;
}

class ElementTree
{
public:
	ElementTree()
	{
	}

	template <typename ElementType, typename PropsType>
	Element* PushElement(PropsType&& props, Widget* owning_widget)
	{
		Element* const element = static_cast<Element*>(PAW_NEW_IN(&element_allocator, ElementType)(PAW_MOVE(props)));
		element->parent = current_parent;
		element->owning_widget = owning_widget;
		if (current_sibling)
		{
			element->sibling = current_sibling->sibling;
			current_sibling->sibling = element;
		}
		else
		{
			element->sibling = current_parent->first_child;
			current_parent->first_child = element;
		}

		current_parent->children_count++;

		current_parent = element;
		current_sibling = nullptr;
		return element;
	}

	void PopElement(Element* element)
	{
		current_sibling = element;
		current_parent = element->parent;
	}

	struct Snapshot
	{
		Element* parent;
		Element* sibling;
	};

	Snapshot GetSnapshot()
	{
		return {current_parent, current_sibling};
	}

	void LoadFromSnapshot(Snapshot const& snapshot)
	{
		current_parent = snapshot.parent;
		current_sibling = snapshot.sibling;
	}

	Element* GetCurrentElement()
	{
		return current_parent;
	}

	void StartFrame()
	{
		element_allocator.FreeAll();
		current_sibling = nullptr;
		current_parent = PAW_NEW_IN(&element_allocator, Element)(Size::fill(), Size::fill());
	}

	void layout(ParentConstraints const& parent_constraints)
	{
		PAW_ASSERT(current_parent, "root element needed");
		current_parent->layout_result = current_parent->layout(parent_constraints);
	}

	void paint(Float2 offset, Float2 size)
	{
		PAW_ASSERT(current_parent, "root element needed");
		current_parent->paint(offset, size);
	}

	void process_mouse_wheel(Float2 wheel_delta)
	{
		PAW_ASSERT(current_parent, "root element needed");
		current_parent->process_mouse_wheel(wheel_delta);
	}

	void process_mouse_button_down(Float2 offset, MouseButton button, Float2 mouse_position)
	{
		PAW_ASSERT(current_parent, "root element needed");
		Element* element = current_parent->hit_test(offset, button, mouse_position);
		if (element != nullptr)
		{
			element->on_mouse_button_down(button, mouse_position);
		}
	}

	void process_mouse_button_up(MouseButton button)
	{
		PAW_ASSERT(current_parent, "root element needed");
		current_parent->process_mouse_button_up(button);
	}

	void process_mouse_move(Float2 offset, Float2 mouse_position)
	{
		PAW_ASSERT(current_parent, "root element needed");
		current_parent->process_mouse_move(offset, mouse_position);
	}

private:
	Element* current_parent = nullptr;
	Element* current_sibling = nullptr;
	Element* child_slot_parent = nullptr;
	Element* child_slot_sibling = nullptr;
	ArenaAllocator element_allocator{};
};

struct WidgetTreeSnapshot
{
	Widget* parent;
	Widget* sibling;
};

struct WidgetBuildContext
{
	void ChildrenSlot(SrcLocation&& src_loc = SrcLoc());

	WidgetTreeSnapshot widget_tree_snapshot;
	ElementTree::Snapshot element_tree_snapshot;
	bool snapshotted = false;
};

struct NullStateType
{
};

template <typename PropsType>
using StatelessWidgetFunctionType = void(WidgetBuildContext&, PropsType&&);

template <typename PropsType, typename StateType>
using StatefulWidgetFunctionType = void(WidgetBuildContext&, PropsType&&, StateType&);

class ScopedWidget
{
public:
	template <typename PropsType, typename Callable, typename StateType>
	static ScopedWidget Create(Callable callable, SrcLocation&& src_loc, PropsType&& props, PtrSize widget_type, char const* debug_type_name)
	{
		constexpr bool stateless = std::is_same<StateType, NullStateType>::value;

		Widget* widget = nullptr;
		Widget* const test_widget = g_current_sibling ? g_current_sibling->sibling : g_current_parent->first_child;
		Widget* prev_child_sibling = g_current_sibling;
		if (test_widget && test_widget->type == widget_type && test_widget->id == g_current_widget_id && test_widget->last_frame_index != g_current_frame_index)
		{
			widget = test_widget;
		}
		else
		{
			// fprintf(stdout, "Widget is different, searching in parent for widget\n");
			prev_child_sibling = nullptr;
			for (widget = g_current_parent->first_child; widget != nullptr; widget = widget->sibling)
			{
				if (widget->type == widget_type && widget->id == g_current_widget_id && widget->last_frame_index != g_current_frame_index)
				{
					// fprintf(stdout, "Found matching widget\n");
					break;
				}

				prev_child_sibling = widget;
			}
			// if (widget == nullptr)
			// {
			// 	fprintf(stdout, "Failed to find matching widget in parent\n");
			// }
		}

		if (widget == nullptr)
		{
			// widget = PAW_NEW_IN(g_widget_allocator, WidgetType)();
			Widget* new_widget = new Widget();
			new_widget->debug_type_name = debug_type_name;
			if constexpr (!stateless)
			{
				StateType* state = new StateType();
				new_widget->state = static_cast<void*>(state);
			}
			else
			{
				new_widget->state = nullptr;
			}
			fprintf(stdout, "Alloc new %s widget %llu\n", debug_type_name, g_new_widget_count++);
			new_widget->parent = g_current_parent;
			new_widget->id = g_current_widget_id;
			new_widget->type = widget_type;

			if (g_current_sibling)
			{
				g_current_sibling->sibling = new_widget;
			}
			else
			{
				g_current_parent->first_child = new_widget;
			}

			widget = static_cast<Widget*>(new_widget);

			g_current_parent->children_count++;
		}
		else if (prev_child_sibling != g_current_sibling)
		{
			// #TODO: Properly remove the widget from the list at the old position and reinsert into the new position
			fprintf(stdout, "Re-order widget\n");
			// remove from list
			if (prev_child_sibling)
			{
				prev_child_sibling->sibling = widget->sibling;
			}
			else
			{
				g_current_parent->first_child = widget->sibling;
			}

			// add back in at new position
			if (g_current_sibling)
			{
				widget->sibling = g_current_sibling->sibling;
				g_current_sibling->sibling = widget;
			}
			else
			{
				if (g_current_parent->first_child)
				{
					widget->sibling = g_current_parent->first_child;
				}
				g_current_parent->first_child = widget;
			}
		}

		widget->last_frame_index = g_current_frame_index;
		widget->last_src_loc = src_loc;

		g_current_parent = widget;
		g_current_sibling = nullptr;

		g_current_widget_id = g_null_widget_id;

		WidgetBuildContext build_context{};

		if constexpr (stateless)
		{
			callable(build_context, PAW_MOVE(props));
		}
		else
		{
			PAW_ASSERT(widget->state != nullptr, "State should not be nullptr here");
			callable(build_context, PAW_MOVE(props), *static_cast<StateType*>(widget->state));
		}
		ElementTree::Snapshot element_tree_snapshot = g_element_tree->GetSnapshot();

		if (build_context.snapshotted)
		{
			g_current_parent = build_context.widget_tree_snapshot.parent;
			g_current_sibling = build_context.widget_tree_snapshot.sibling;
			g_element_tree->LoadFromSnapshot(build_context.element_tree_snapshot);
		}
		else
		{
			g_current_parent = widget;
			g_current_sibling = nullptr;
		}

		return {widget, PAW_MOVE(build_context), PAW_MOVE(element_tree_snapshot)};
	}

	template <typename PropsType>
	static ScopedWidget CreateWrapper(StatelessWidgetFunctionType<PropsType>* func, SrcLocation&& src_loc, PropsType&& props, char const* debug_type_name)
	{
		return ScopedWidget::Create<PropsType, decltype(func), NullStateType>(func, PAW_MOVE(src_loc), PAW_MOVE(props), reinterpret_cast<PtrSize>(func), debug_type_name);
	}

	template <typename PropsType, typename StateType>
	static ScopedWidget CreateWrapper(StatefulWidgetFunctionType<PropsType, StateType>* func, SrcLocation&& src_loc, PropsType&& props, char const* debug_type_name)
	{
		return ScopedWidget::Create<PropsType, decltype(func), StateType>(func, PAW_MOVE(src_loc), PAW_MOVE(props), reinterpret_cast<PtrSize>(func), debug_type_name);
	}

	ScopedWidget(Widget* widget, WidgetBuildContext&& build_context, ElementTree::Snapshot&& element_tree_snapshot)
		: widget(widget)
		, build_context(PAW_MOVE(build_context))
		, element_tree_snapshot(PAW_MOVE(element_tree_snapshot))
	{
	}

	~ScopedWidget()
	{
		g_current_sibling = widget;
		g_current_parent = widget->parent;
		g_element_tree->LoadFromSnapshot(element_tree_snapshot);
	}

	bool WantsChildren()
	{
		bool result = build_context.snapshotted;
		build_context.snapshotted = false;
		return result;
	}

	Widget* GetWidget() const
	{
		return widget;
	}

private:
	Widget* const widget;
	WidgetBuildContext build_context{};
	ElementTree::Snapshot element_tree_snapshot;
};

Widget* GetCurrentWidget()
{
	return g_current_parent;
}

Widget* GetCurrentSibling()
{
	return g_current_sibling;
}

struct ChildrenSlotProps
{
};

static void ChildrenSlotWidget(WidgetBuildContext&, ChildrenSlotProps&&)
{
}

void WidgetBuildContext::ChildrenSlot(SrcLocation&& src_loc)
{
	ScopedWidget scoped_widget = ScopedWidget::Create<ChildrenSlotProps, decltype(ChildrenSlotWidget), NullStateType>(ChildrenSlotWidget, PAW_MOVE(src_loc), {}, reinterpret_cast<PtrSize>(&ChildrenSlotWidget), "Children Slot Widget");
	widget_tree_snapshot = {g_current_parent, g_current_sibling};
	element_tree_snapshot = g_element_tree->GetSnapshot();
	snapshotted = true;
}

#define WIDGET_RAW(WidgetFuncType, ...) \
	for (ScopedWidget PAW_CONCAT(i, __LINE__) = ScopedWidget::CreateWrapper(WidgetFuncType, SrcLoc(), __VA_ARGS__, #WidgetFuncType); PAW_CONCAT(i, __LINE__).WantsChildren();)

#define STATELESS_WIDGET(WidgetFuncType, ...) WIDGET_RAW(WidgetFuncType, __VA_ARGS__)
#define STATEFUL_WIDGET(WidgetFuncType, ...) WIDGET_RAW(WidgetFuncType, __VA_ARGS__)

#define WIDGET_ID(id)                                                         \
	WidgetTempID PAW_CONCAT(widget_temp_id_, __LINE__) = begin_widget_id(id); \
	for (int PAW_CONCAT(i, __LINE__) = 0; PAW_CONCAT(i, __LINE__) < 1; (PAW_CONCAT(i, __LINE__)++), (end_widget_id(PAW_CONCAT(widget_temp_id_, __LINE__))))

struct FlexItemDesc
{
	F32 factor = 1.0f;
};

class FlexItem : public Element
{
public:
	FlexItem(FlexItemDesc&& desc)
		: Element(Size::fill(), Size::fill())
	{
		factor = desc.factor;
	}

	static ClassInfo const& GetStaticTypeInfo()
	{
		static ClassInfo class_info{&Element::GetStaticTypeInfo()};
		return class_info;
	}

	ClassInfo const& GetTypeInfo() const override
	{
		return GetStaticTypeInfo();
	}

	F32 factor;
};

static void FlexItemWidget(WidgetBuildContext& build_context, FlexItemDesc&& props)
{
	Element* element = g_element_tree->PushElement<FlexItem>(PAW_MOVE(props), GetCurrentWidget());
	build_context.ChildrenSlot();
	g_element_tree->PopElement(element);
}

#define FLEX_ITEM(...) STATELESS_WIDGET(FlexItemWidget, __VA_ARGS__)

enum class Orientation
{
	Horizontal,
	Vertical,
};

enum class FlexAligment
{
	Start,
	Center,
	End,
};

struct FlexBoxDesc
{
	Orientation orientation = Orientation::Horizontal;
	FlexAligment alignment = FlexAligment::Start;
	Size width = Size::shrink_wrap();
	Size height = Size::shrink_wrap();
	F32 gap = 0.0f;
};

class FlexBox : public Element
{
public:
	FlexBox(FlexBoxDesc&& desc)
		: Element(desc.width, desc.height)
	{
		orientation = desc.orientation;
		alignment = desc.alignment;
		gap = desc.gap;
	}

	static ClassInfo const& GetStaticTypeInfo()
	{
		static ClassInfo class_info{&Element::GetStaticTypeInfo()};
		return class_info;
	}

	ClassInfo const& GetTypeInfo() const override
	{
		return GetStaticTypeInfo();
	}

	Float2 layout(ParentConstraints const& parent_constraints) override
	{
		F32 const children_count_f = static_cast<F32>(children_count);

		Float2 size = calc_size(parent_constraints);

		Float2 total_fixed_size = Float2{};
		Float2 max_child_size{};
		F32 total_flex_factor = 0.0f;
		U8 const orientation_index = static_cast<U8>(orientation);
		PAW_ASSERT(orientation_index <= 1, "Invalid flex box orientation");

		F32 fixed_size = size[orientation_index];

		for (Element* child = first_child; child != nullptr; child = child->sibling)
		{
			if (child->GetTypeInfo().IsDerivedFrom(FlexItem::GetStaticTypeInfo()))
			{
				FlexItem* item = static_cast<FlexItem*>(child);
				total_flex_factor += item->factor;
			}
			else
			{
				ParentConstraints fixed_constraints{parent_constraints.min_size, size};
				fixed_constraints.max_size[orientation_index] = fixed_size;

				Float2 const child_size = child->layout(fixed_constraints);
				child->layout_result = child_size;

				total_fixed_size += child_size;
				fixed_size -= child_size[orientation_index] + gap;
				max_child_size.x = Max(max_child_size.x, child_size.x);
				max_child_size.y = Max(max_child_size.y, child_size.y);
			}
		}

		Float2 total_gap{};
		total_gap[orientation_index] = (children_count_f - 1.0f) * gap;
		Size const sizes[] = {width, height};

		if (sizes[orientation_index].type == Size::Type::ShrinkWrap)
		{
			size[orientation_index] = total_fixed_size[orientation_index] + total_gap[orientation_index];
		}

		if (sizes[1 - orientation_index].type == Size::Type::ShrinkWrap)
		{
			size[1 - orientation_index] = max_child_size[1 - orientation_index];
		}

		Float2 const size_without_gap = size - total_gap;

		Float2 const size_per_factor =
			total_flex_factor > 0.0f ? (size_without_gap - total_fixed_size) / total_flex_factor : Float2{};
		Float2 const total_content_size = total_fixed_size + (size_per_factor * total_flex_factor) + total_gap;

		PAW_ASSERT(
			!(LengthSquared(total_fixed_size) == 0.0f &&
			  (width.type == Size::Type::ShrinkWrap || height.type == Size::Type::ShrinkWrap)),
			"Flex does not support using ShrinkWrap when there is no fixed size content");

		PAW_ASSERT(!(total_flex_factor > 0.0f && sizes[orientation_index].type == Size::Type::ShrinkWrap), "Flex does not support ShrinkWrap in the primary dimension when containing flex items");

		Float2 offset = Float2{};
		switch (alignment)
		{
			case FlexAligment::Start:
				break;
			case FlexAligment::Center:
				offset = (size - total_content_size) * 0.5f;
				break;

			case FlexAligment::End:
				offset = size - total_content_size;
				break;
		}

		for (Element* child = first_child; child != nullptr; child = child->sibling)
		{
			if (child->GetTypeInfo().IsDerivedFrom(FlexItem::GetStaticTypeInfo()))
			{
				FlexItem* flex = static_cast<FlexItem*>(child);
				Float2 const child_sizes[] = {
					Float2{size_per_factor.x * flex->factor, size_without_gap.y},
					Float2{size_without_gap.x, size_per_factor.y * flex->factor},
				};
				Float2 const child_size = child->layout(
					ParentConstraints{
						.min_size = Float2{},
						.max_size = child_sizes[orientation_index],
					});
				child->layout_result = child_size;

				max_child_size.x = Max(max_child_size.x, child_size.x);
				max_child_size.y = Max(max_child_size.y, child_size.y);
			}

			child->layout_offset[orientation_index] = offset[orientation_index];
			offset[orientation_index] += child->layout_result[orientation_index] + gap;
		}

		return size;
	}

	/*Float2 layout(ParentConstraints const& parent_constraints) override
	{
		Float2 const size = calc_size(parent_constraints);
		F32 offset = 0.0f;
		for (Element* child = first_child; child != nullptr; child = child->sibling)
		{
			bool is_flex_item = child->GetTypeInfo().IsDerivedFrom(FlexItem::GetStaticTypeInfo());
			(void)is_flex_item;
			ParentConstraints const constraints{
				.min = Float2{},
				.max = Float2{size.x, size.y / static_cast<F32>(children_count)},
			};
			Float2 const child_size = child->layout(constraints);
			child->layout_result = child_size;
			child->layout_offset = Float2{0.0f, offset};

			offset += child_size.y;
		}
		return size;
	}*/

private:
	Orientation orientation;
	FlexAligment alignment;
	F32 gap;
};

static void FlexBoxWidget(WidgetBuildContext& build_context, FlexBoxDesc&& props)
{
	Element* element = g_element_tree->PushElement<FlexBox>(PAW_MOVE(props), GetCurrentWidget());
	build_context.ChildrenSlot();
	g_element_tree->PopElement(element);
}

#define FLEX_BOX(...) STATELESS_WIDGET(FlexBoxWidget, __VA_ARGS__)

// struct GridDesc_t
//{
//	Size width = Size::fill();
//	Size height = Size::fill();
//	S32 row_count = -1;
//	S32 column_count = -1;
//	F32 gap = 0.0f;
// };
//
// class Grid : public Widget
//{
// public:
//	Grid(GridDesc_t&& desc)
//		: Widget(desc.width, desc.height), row_count(row_count), column_count(column_count), gap(desc.gap)
//	{
//	}
//
//	static ClassInfo const& GetStaticTypeInfo()
//	{
//		static ClassInfo class_info{&Widget::GetStaticTypeInfo()};
//		return class_info;
//	}
//
//	ClassInfo const& GetTypeInfo() const override
//	{
//		return GetStaticTypeInfo();
//	}
//
//	Float2 layout(ParentConstraints const& parent_constraints) override
//	{
//		F32 const children_count_f = static_cast<F32>(children_count);
//
//		F32 row_count_f = 0;
//		if (row_count == -1)
//		{
//			row_count_f = children_count_f / static_cast<F32>(column_count));
//		}
//		else
//		{
//			row_count_f = static_cast<F32>(row_count);
//		}
//
//		F32 column_count_f = 0.0f;
//		if (column_count == -1)
//		{
//			column_count_f = children_count_f / row_count_f);
//		}
//		else
//		{
//			column_count_f = static_cast<F32>(column_count);
//		}
//
//		Float2 const total_size = calc_size(parent_constraints);
//
//		Float2 const size = total_size - Float2{(column_count_f - 1.0f) * gap, (row_count_f - 1.0f) * gap};
//
//		F32 column_size = size.x / column_count_f;
//		F32 row_size = size.y / row_count_f;
//
//		ParentConstraints new_constraints{
//			.min = Float2{},
//			.max = Float2{column_size, row_size},
//		};
//
//		F32 i = 0.0f;
//		for (Widget* child = first_child; child != nullptr; child = child->sibling)
//		{
//			ctx.layoutWidget(child, new_constraints);
//			F32 const x = Math::remainder(i, column_count_f);
//			F32 const y = Math::floor(i / column_count_f);
//
//			child->layout_offset = Float2{
//				.x = x > 0 ? x * (column_size + gap) : 0.0f,
//				.y = y > 0 ? y * (row_size + gap) : 0.0f,
//			};
//			i += 1.0f;
//		}
//
//		return total_size;
//	}
//
// private:
//	S32 const row_count;
//	S32 const column_count;
//	F32 const gap;
// };

struct ForegroundPainterDesc
{
};

class ForegroundPainter : public Element
{
public:
	ForegroundPainter(ForegroundPainterDesc&&)
		: Element(Size::fill(), Size::fill())
	{
	}

	void paint(Float2 offset, Float2 size) override
	{
		WidgetContext* previous_context = g_widget_context;
		g_widget_context = &g_foreground_widget_context;

		Element::paint(offset, size);

		g_widget_context = previous_context;
	}
};

static void ForegroundPainterWidget(WidgetBuildContext& build_context, ForegroundPainterDesc&& props)
{
	Element* element = g_element_tree->PushElement<ForegroundPainter>(PAW_MOVE(props), GetCurrentWidget());
	build_context.ChildrenSlot();
	g_element_tree->PopElement(element);
}

#define FOREGROUND_PAINTER(...) STATELESS_WIDGET(ForegroundPainterWidget, __VA_ARGS__)

struct RelativeOffsetDesc
{
	Offset x = Offset::pixel(0.0f);
	Offset y = Offset::pixel(0.0f);
};

class RelativeOffset : public Element
{
public:
	RelativeOffset(RelativeOffsetDesc&& desc)
		: Element(Size::shrink_wrap(), Size::shrink_wrap())
	{
		x = desc.x;
		y = desc.y;
	}

	Float2 layout(ParentConstraints const& parent_constraints) override
	{

		Float2 const child_size = Element::layout(parent_constraints);

		F32 x_value = 0.0f;
		switch (x.type)
		{
			case Offset::Type::Pixel:
			{
				x_value = x.value;
			}
			break;

			case Offset::Type::ParentSizeRelative:
			{
				x_value = parent_constraints.max_size.x * x.value;
			}
			break;

			case Offset::Type::ChildSizeRelative:
			{
				x_value = child_size.x * x.value;
			}
			break;
		}

		F32 y_value = 0.0f;
		switch (y.type)
		{
			case Offset::Type::Pixel:
			{
				y_value = y.value;
			}
			break;

			case Offset::Type::ParentSizeRelative:
			{
				y_value = parent_constraints.max_size.y * y.value;
			}
			break;

			case Offset::Type::ChildSizeRelative:
			{
				y_value = child_size.y * y.value;
			}
			break;
		}

		for (Element* child = first_child; child != nullptr; child = child->sibling)
		{
			child->layout_offset += Float2{x_value, y_value};
		}
		// return calc_size(parent_constraints);
		return child_size;
	}

private:
	Offset x;
	Offset y;
};

static void RelativeOffsetWidget(WidgetBuildContext& build_context, RelativeOffsetDesc&& props)
{
	Element* element = g_element_tree->PushElement<RelativeOffset>(PAW_MOVE(props), GetCurrentWidget());
	build_context.ChildrenSlot();
	g_element_tree->PopElement(element);
}

#define RELATIVE_OFFSET(...) STATELESS_WIDGET(RelativeOffsetWidget, __VA_ARGS__)

struct CursorInteractDesc
{
	// This will not be stored past the callsite
	bool* out_hovered = nullptr;
	bool* out_currently_clicked = nullptr;
	bool* out_click_finished = nullptr;
};

class CursorInteract : public Element
{
public:
	CursorInteract(CursorInteractDesc&& desc)
		: Element(Size::shrink_wrap(), Size::shrink_wrap())
	{
		if (desc.out_hovered)
		{
			*desc.out_hovered = g_last_frame_hovered_element_owner == owning_widget;
		}
		if (desc.out_currently_clicked)
		{
			*desc.out_currently_clicked = g_active_element_owner == owning_widget;
		}

		bool const just_clicked = g_just_clicked_element_owner == owning_widget;

		if (desc.out_click_finished)
		{
			*desc.out_click_finished = just_clicked;
		}
		if (just_clicked)
		{
			g_just_clicked_element_owner = nullptr;
		}
	}

	void process_mouse_move(Float2 offset, Float2 mouse_position) override
	{
		Float2 const position = offset + layout_offset;
		if (mouse_position.x >= position.x && mouse_position.y >= position.y && mouse_position.x < position.x + layout_result.x && mouse_position.y < position.y + layout_result.y)
		{
			g_widget_context->push_hovered(owning_widget);
		}

		Element::process_mouse_move(position, mouse_position);
	}

	void paint(Float2 offset, Float2 size) override
	{
		/*(*g_current_cursor_interact_box_list)[(*g_current_cursor_interact_box_index)++] = CursorInteractBox_t{
			.widget = this,
			.position = offset,
			.size = size,
		};*/
		Element::paint(offset, size);
	}

	void on_mouse_button_down(MouseButton button, Float2 /* mouse_position */) override
	{
		if (button == MouseButton::Left)
		{
			if (g_last_frame_hovered_element_owner == owning_widget)
			{
				g_active_element_owner = owning_widget;
				// return EventState::Handled;
			}
		}
		// return EventState::Propagate;
	}

	void process_mouse_button_up(MouseButton button) override
	{
		if (button == MouseButton::Left)
		{
			if (g_active_element_owner == owning_widget)
			{
				if (g_last_frame_hovered_element_owner == owning_widget)
				{
					g_just_clicked_element_owner = owning_widget;
				}
				else
				{
					g_active_element_owner = nullptr;
				}
			}
		}
	}
};

static void CursorInteractWidget(WidgetBuildContext& build_context, CursorInteractDesc&& props)
{
	Element* element = g_element_tree->PushElement<CursorInteract>(PAW_MOVE(props), GetCurrentWidget());
	build_context.ChildrenSlot();
	g_element_tree->PopElement(element);
}

#define CURSOR_INTERACT(...) STATEFUL_WIDGET(CursorInteractWidget, __VA_ARGS__)

class BoxElement : public Element
{
public:
	struct Props
	{
		Size width = Size::shrink_wrap();
		Size height = Size::shrink_wrap();
		Float4 color{1.0f, 1.0f, 1.0f, 1.0f};
	};

	BoxElement(BoxElement::Props&& props)
		: Element(props.width, props.height)
		, color(props.color)
	{
	}

	static ClassInfo const& GetStaticTypeInfo()
	{
		static ClassInfo class_info{&Element::GetStaticTypeInfo()};
		return class_info;
	}

	ClassInfo const& GetTypeInfo() const override
	{
		return GetStaticTypeInfo();
	}

	void paint(Float2 offset, Float2 size) override
	{
		g_widget_context->push_render_box(RenderBox{
			.position = offset,
			.size = size,
			.color = color,
		});
		Element::paint(offset, size);
	}

private:
	Float4 const color;
};

static void BoxWidget(WidgetBuildContext& build_context, BoxElement::Props&& props)
{
	Element* element = g_element_tree->PushElement<BoxElement>(PAW_MOVE(props), GetCurrentWidget());
	build_context.ChildrenSlot();
	g_element_tree->PopElement(element);
}

#define BOX(...) STATELESS_WIDGET(BoxWidget, __VA_ARGS__)

struct PaddingProps
{
	F32 left = 0.0f;
	F32 top = 0.0f;
	F32 right = 0.0f;
	F32 bottom = 0.0f;

	static PaddingProps All(F32 value)
	{
		return {value, value, value, value};
	}
};

class Padding : public Element
{
public:
	Padding(PaddingProps&& desc)
		: Element()
	{
		left = desc.left;
		top = desc.top;
		right = desc.right;
		bottom = desc.bottom;
	}

	static ClassInfo const& GetStaticTypeInfo()
	{
		static ClassInfo class_info{&Element::GetStaticTypeInfo()};
		return class_info;
	}

	ClassInfo const& GetTypeInfo() const override
	{
		return GetStaticTypeInfo();
	}

	Float2 layout(ParentConstraints const& parent_constraints) override
	{
		Float2 const total_padding{left + right, top + bottom};
		ParentConstraints new_constraints = parent_constraints;
		new_constraints.max_size -= total_padding;

		Float2 const size = Element::layout(new_constraints);
		for (Element* child = first_child; child != nullptr; child = child->sibling)
		{
			child->layout_offset = Float2{left, top};
		}
		return size + total_padding;
	}

private:
	F32 left;
	F32 top;
	F32 right;
	F32 bottom;
};

static void PaddingWidget(WidgetBuildContext& build_context, PaddingProps&& props)
{
	Element* element = g_element_tree->PushElement<Padding>(PAW_MOVE(props), GetCurrentWidget());
	build_context.ChildrenSlot();
	g_element_tree->PopElement(element);
}

#define PADDING(...) STATELESS_WIDGET(PaddingWidget, __VA_ARGS__)

struct ClipRectProps
{
};

class ClipRect : public Element
{
public:
	ClipRect(ClipRectProps&& /*desc*/)
		: Element()
	{
	}

	static ClassInfo const& GetStaticTypeInfo()
	{
		static ClassInfo class_info{&Element::GetStaticTypeInfo()};
		return class_info;
	}

	ClassInfo const& GetTypeInfo() const override
	{
		return GetStaticTypeInfo();
	}

	void paint(Float2 offset, Float2 size) override
	{
		RenderClipRect const previous_clip_rect = g_widget_context->push_clip_rect(RenderClipRect{
			.position = offset,
			.size = size,
		});
		Element::paint(offset, size);
		g_widget_context->push_clip_rect(previous_clip_rect);
	}

private:
};

static void ClipRectWidget(WidgetBuildContext& build_context, ClipRectProps&& props)
{
	Element* element = g_element_tree->PushElement<ClipRect>(PAW_MOVE(props), GetCurrentWidget());
	build_context.ChildrenSlot();
	g_element_tree->PopElement(element);
}

#define CLIP_RECT(...) STATEFUL_WIDGET(ClipRectWidget, __VA_ARGS__)

struct ScrollViewDesc
{
	Size width = Size::fill();
	Size height = Size::fill();
	F32* scroll_amount = nullptr;
	F32* bar_ratio = nullptr;
};

class ScrollViewElement : public Element
{
public:
	ScrollViewElement(ScrollViewDesc&& desc)
		: Element(desc.width, desc.height)
		, scroll_amount(desc.scroll_amount)
		, bar_ratio(desc.bar_ratio)
	{
		PAW_ASSERT(scroll_amount != nullptr, "ScrollView requires a scroll_amount pointer");
		PAW_ASSERT(width.type != Size::Type::ShrinkWrap, "ScrollView does not support ShrinkWrap");
		PAW_ASSERT(height.type != Size::Type::ShrinkWrap, "ScrollView does not support ShrinkWrap");
	}

	static ClassInfo const& GetStaticTypeInfo()
	{
		static ClassInfo class_info{&Element::GetStaticTypeInfo()};
		return class_info;
	}

	ClassInfo const& GetTypeInfo() const override
	{
		return GetStaticTypeInfo();
	}

	F32 calc_overflow(Float2 const& size) const
	{
		F32 max_child_height = 0.0f;
		for (Element* child = first_child; child != nullptr; child = child->sibling)
		{
			max_child_height = Max(child->layout_result.y, max_child_height);
		}
		return Max(max_child_height - size.y, 0.0f);
	}

	void clamp_scroll_amount(Float2 const& size)
	{
		*scroll_amount = Clamp(*scroll_amount, 0.0f, calc_overflow(size));
	}

	void process_mouse_wheel(Float2 wheel_delta) override
	{
		if (g_last_frame_hovered_element_owner == owning_widget)
		{
			*scroll_amount += wheel_delta.y;
			clamp_scroll_amount(layout_result);
		}
		else
		{
			Element::process_mouse_wheel(wheel_delta);
		}
	}

	void process_mouse_move(Float2 offset, Float2 mouse_position) override
	{
		Float2 const position = offset + layout_offset;
		if (mouse_position.x >= position.x && mouse_position.y >= position.y && mouse_position.x < position.x + layout_result.x && mouse_position.y < position.y + layout_result.y)
		{
			g_widget_context->push_hovered(owning_widget);
		}

		Element::process_mouse_move(position, mouse_position);
	}

	Float2 layout(ParentConstraints const& parent_constraints) override
	{
		Element::layout(parent_constraints);
		for (Element* child = first_child; child != nullptr; child = child->sibling)
		{
			child->layout_offset.y = -*scroll_amount;
		}
		return calc_size(parent_constraints);
	}

	void paint(Float2 offset, Float2 size) override
	{
		/*(*g_current_cursor_interact_box_list)[(*g_current_cursor_interact_box_index)++] = CursorInteractBox_t{
			.widget = this,
			.position = offset,
			.size = size,
		};*/

		clamp_scroll_amount(size); // Clamp in paint so when we re-paint from resize or whatever, the scroll amount doesn't go cazy

		Element::paint(offset, size);
		F32 const overflow = calc_overflow(size);
		if (overflow > 0.0f)
		{
			F32 const ratio = 1.0f - (overflow / size.y);
			if (bar_ratio)
			{
				*bar_ratio = ratio;
			}
		}
		else if (bar_ratio)
		{
			*bar_ratio = 0;
		}
	}

private:
	F32* const scroll_amount;
	F32* const bar_ratio;
};

static void ScrollViewRootWidget(WidgetBuildContext& build_context, ScrollViewDesc&& props)
{
	Element* element = g_element_tree->PushElement<ScrollViewElement>(PAW_MOVE(props), GetCurrentWidget());
	build_context.ChildrenSlot();
	g_element_tree->PopElement(element);
}

#define SCROLL_VIEW_ELEMENT(...) STATELESS_WIDGET(ScrollViewRootWidget, __VA_ARGS__)

struct ScrollViewProps
{
};

struct ScrollViewState
{
	F32 scroll_amount = 0.0f;
	F32 bar_ratio = 0.0f;
};

static void ScrollViewWidget(WidgetBuildContext& build_context, ScrollViewProps&&, ScrollViewState& state)
{
	FLEX_BOX({.orientation = Orientation::Horizontal, .width = Size::fill(), .height = Size::fill(), .gap = 2.0f})
	{
		FLEX_ITEM({})
		{
			SCROLL_VIEW_ELEMENT({.scroll_amount = &state.scroll_amount, .bar_ratio = &state.bar_ratio})
			{
				build_context.ChildrenSlot();
			}
		}
		if (state.bar_ratio > 0.0f)
		{
			BOX({.width = Size::pixel(5.0f), .height = Size::fill()})
			{
				RELATIVE_OFFSET({.y = Offset::pixel(state.scroll_amount)})
				{
					BOX({.width = Size::fill(), .height = Size::fractional(state.bar_ratio), .color = rgb(55, 44, 100)});
				}
			}
		}
	}
}

#define SCROLL_VIEW(...) STATEFUL_WIDGET(ScrollViewWidget, __VA_ARGS__)

enum class SliderOrientation
{
	LeftToRight,
	RightToLeft,
	TopToBottom,
	BottomToTop,
};

struct SliderState
{
	F32 value = 0.0f;
};

struct SliderDesc
{
	Size width = Size::fill();
	Size height = Size::fill();
	SliderState* state = nullptr;
	F32 min_value = 0.0f;
	F32 max_value = 1.0f;
	SliderOrientation orientation = SliderOrientation::LeftToRight;
};

class Slider : public Element
{
public:
	Slider(SliderDesc&& desc)
		: Element(desc.width, desc.height)
	{
		state = desc.state;
		min_value = desc.min_value;
		max_value = desc.max_value;
		orientation = desc.orientation;
	}

	static ClassInfo const& GetStaticTypeInfo()
	{
		static ClassInfo class_info{&Element::GetStaticTypeInfo()};
		return class_info;
	}

	ClassInfo const& GetTypeInfo() const override
	{
		return GetStaticTypeInfo();
	}

	void on_mouse_button_down(MouseButton button, Float2 /* mouse_position */) override
	{
		if (button == MouseButton::Left)
		{
			if (g_last_frame_hovered_element_owner == owning_widget)
			{
				g_active_element_owner = owning_widget;
				// return EventState::Handled;
			}
		}
		// return EventState::Handled;
	}

	void process_mouse_button_up(MouseButton button) override
	{
		if (button == MouseButton::Left)
		{
			if (g_active_element_owner == owning_widget)
			{
				if (g_last_frame_hovered_element_owner == owning_widget)
				{
					g_just_clicked_element_owner = owning_widget;
				}
				else
				{
					g_active_element_owner = nullptr;
				}
			}
		}
	}

	void process_mouse_wheel(Float2 /*wheel_delta*/) override
	{
	}

	void process_mouse_move(Float2 offset, Float2 mouse_position) override
	{
		Float2 const position = offset + layout_offset;
		Float2 const size = layout_result;
		if (g_active_element_owner == owning_widget)
		{
			Float2 line_start{};
			Float2 line_end{};
			switch (orientation)
			{
				case SliderOrientation::LeftToRight:
				{
					line_start = position + Float2{0.0f, size.y * 0.5f};
					line_end = position + Float2{size.x, size.y * 0.5f};
				}
				break;

				case SliderOrientation::RightToLeft:
				{
					line_start = position + Float2{size.x, size.y * 0.5f};
					line_end = position + Float2{0.0f, size.y * 0.5f};
				}
				break;

				case SliderOrientation::TopToBottom:
				{
					line_start = position + Float2{size.x * 0.5f, 0.0f};
					line_end = position + Float2{size.x * 0.5f, size.y};
				}
				break;

				case SliderOrientation::BottomToTop:
				{
					line_start = position + Float2{size.x * 0.5f, size.y};
					line_end = position + Float2{size.x * 0.5f, 0.0f};
				}
				break;
			}

			F32 const line_len = Length(line_end - line_start);
			Float2 const relative_mouse = mouse_position - line_start;
			F32 const projected_mouse = Dot(relative_mouse, Normalize(line_end - line_start)) / line_len;
			F32 const value_range = max_value - min_value;
			state->value = min_value + value_range * projected_mouse;

			clamp_value();
		}

		if (mouse_position.x >= position.x && mouse_position.y >= position.y && mouse_position.x < position.x + layout_result.x && mouse_position.y < position.y + layout_result.y)
		{
			g_widget_context->push_hovered(owning_widget);
		}

		Element::process_mouse_move(position, mouse_position);
	}

	static constexpr F32 scrollbar_width = 5.0f;

	void clamp_value()
	{
		state->value = Clamp(state->value, min_value, max_value);
	}

	Float2 layout(ParentConstraints const& parent_constraints) override
	{
		Float2 const size = calc_size(parent_constraints);
		ParentConstraints const constraints{
			parent_constraints.min_size,
			size,
		};

		Element::layout(constraints);
		return size;
	}

	void paint(Float2 offset, Float2 size) override
	{
		g_widget_context->push_render_box({
			.position = offset,
			.size = size,
			.color = rgb(150, 0, 0),
		});

		clamp_value();

		F32 const value_range = max_value - min_value;
		F32 const value_ratio = (state->value - min_value) / value_range;

		Float2 draw_position{};
		Float2 draw_size{};
		switch (orientation)
		{
			case SliderOrientation::LeftToRight:
			{
				draw_position = offset;
				draw_size = Float2{size.x * value_ratio, size.y};
			}
			break;

			case SliderOrientation::RightToLeft:
			{
				draw_position = offset + Float2{size.x * (1.0f - value_ratio), 0.0f};
				draw_size = Float2{size.x * value_ratio, size.y};
			}
			break;

			case SliderOrientation::TopToBottom:
			{
				draw_position = offset;
				draw_size = Float2{size.x, size.y * value_ratio};
			}
			break;

			case SliderOrientation::BottomToTop:
			{
				draw_position = offset + Float2{0.0f, size.y * (1.0f - value_ratio)};
				draw_size = Float2{size.x, size.y * value_ratio};
			}
			break;
		}

		g_widget_context->push_render_box({
			.position = draw_position,
			.size = draw_size,
			.color = rgb(255, 255, 255),
		});
	}

private:
	SliderState* state;
	F32 min_value;
	F32 max_value;
	SliderOrientation orientation;
};

static void SliderWidget(WidgetBuildContext& build_context, SliderDesc&& props)
{
	Element* element = g_element_tree->PushElement<Slider>(PAW_MOVE(props), GetCurrentWidget());
	build_context.ChildrenSlot();
	g_element_tree->PopElement(element);
}

#define SLIDER(...) STATELESS_WIDGET(SliderWidget, __VA_ARGS__)

enum class TabBarOrientation
{
	LeftToRight,
	RightToLeft,
	TopToBottom,
	BottomToTop,
};

struct TabState
{
	char const* name;
	Float4 color{1.0f, 1.0f, 1.0f, 1.0f};
	TabState* next_sibling = nullptr;
	TabState* prev_sibling = nullptr;
	Element* widget = nullptr;
};

struct TabBarState
{
	F32 value = 0.0f;
	F32 active_tab_click_offset = 0.0f;
	// Widget* active_tab = nullptr;
	TabState* tab_list = nullptr;
	F32 value_before_active = 0.0f;
};

struct TabBarDesc
{
	Size width = Size::fill();
	Size height = Size::fill();
	TabBarState* state = nullptr;
	TabBarOrientation orientation = TabBarOrientation::LeftToRight;
};

class TabBar : public Element
{
public:
	TabBar(TabBarDesc&& desc)
		: Element(desc.width, desc.height)
	{
		state = desc.state;
		orientation = desc.orientation;
		PAW_ASSERT(desc.state != nullptr, "TabBar requires state pointer to be set");
	}

	static ClassInfo const& GetStaticTypeInfo()
	{
		static ClassInfo class_info{&Element::GetStaticTypeInfo()};
		return class_info;
	}

	ClassInfo const& GetTypeInfo() const override
	{
		return GetStaticTypeInfo();
	}

	F32 project_mouse_onto_line(Float2 position, Float2 size, Float2 mouse_position) const
	{
		Float2 line_start{};
		Float2 line_end{};
		switch (orientation)
		{
			case TabBarOrientation::LeftToRight:
			{
				line_start = position + Float2{0.0f, size.y * 0.5f};
				line_end = position + Float2{size.x, size.y * 0.5f};
			}
			break;

			case TabBarOrientation::RightToLeft:
			{
				line_start = position + Float2{size.x, size.y * 0.5f};
				line_end = position + Float2{0.0f, size.y * 0.5f};
			}
			break;

			case TabBarOrientation::TopToBottom:
			{
				line_start = position + Float2{size.x * 0.5f, 0.0f};
				line_end = position + Float2{size.x * 0.5f, size.y};
			}
			break;

			case TabBarOrientation::BottomToTop:
			{
				line_start = position + Float2{size.x * 0.5f, size.y};
				line_end = position + Float2{size.x * 0.5f, 0.0f};
			}
			break;
		}

		F32 const line_len = Length(line_end - line_start);
		Float2 const relative_mouse = mouse_position - line_start;
		F32 const projected_position = Dot(relative_mouse, Normalize(line_end - line_start)) / line_len;
		return projected_position;
	}

	F32 calc_child_value(Float2 position, Float2 size, Element* child)
	{
		Float2 line_start{};
		Float2 line_end{};
		switch (orientation)
		{
			case TabBarOrientation::LeftToRight:
			{
				line_start = position + Float2{0.0f, size.y * 0.5f};
				line_end = position + Float2{size.x, size.y * 0.5f};
			}
			break;

			case TabBarOrientation::RightToLeft:
			{
				line_start = position + Float2{size.x, size.y * 0.5f};
				line_end = position + Float2{0.0f, size.y * 0.5f};
			}
			break;

			case TabBarOrientation::TopToBottom:
			{
				line_start = position + Float2{size.x * 0.5f, 0.0f};
				line_end = position + Float2{size.x * 0.5f, size.y};
			}
			break;

			case TabBarOrientation::BottomToTop:
			{
				line_start = position + Float2{size.x * 0.5f, size.y};
				line_end = position + Float2{size.x * 0.5f, 0.0f};
			}
			break;
		}

		F32 const line_len = Length(line_end - line_start);
		Float2 const relative_mouse = (position + child->layout_offset) - line_start;
		F32 const projected_position = Dot(relative_mouse, Normalize(line_end - line_start)) / line_len;
		return projected_position;
	}

	void on_mouse_button_down(MouseButton /* button */, Float2 /* mouse_position */) override
	{
// #TODO: re-enable this
#if 0
		Float2 const position = offset + layout_offset;

		if (button == MouseButton::Left)
		{
			for (TabState* tab = state->tab_list; tab != nullptr; tab = tab->next_sibling)
			{
				Element* const child = tab->widget;
				if (g_active_element_owner == child->owning_widget)
				{
					fprintf(stdout, "Changing active tab to %llu\n", reinterpret_cast<U64>(g_active_element_owner));
					F32 const value = project_mouse_onto_line(position, layout_result, mouse_position);
					F32 const child_value = calc_child_value(position, layout_result, child);
					state->value_before_active = child_value;
					state->active_tab_click_offset = value - child_value;
					state->value = child_value;
					// return EventState::Handled;
				}
			}
		}
#endif

		// return EventState::Propagate;
	}

	// void process_mouse_button_up(MouseButton button) override
	//{
	//	if (button == MouseButton::Left)
	//	{
	//		if (g_active_widget == this)
	//		{
	//			if (g_last_frame_hovered == this)
	//			{
	//				g_just_clicked_widget = this;
	//			}
	//			else
	//			{
	//				g_active_widget = nullptr;
	//			}
	//		}
	//	}
	// }

	void process_mouse_move(Float2 offset, Float2 mouse_position) override
	{
		Float2 const position = offset + layout_offset;
		Float2 const size = layout_result;
		TabState* active_tab = nullptr;
		for (TabState* tab = state->tab_list; tab != nullptr; tab = tab->next_sibling)
		{
			Element* const child = tab->widget;
			if (g_active_element_owner == child->owning_widget)
			{
				active_tab = tab;
				// fprintf(stdout, "Active Tab: %s\n", tab->name);
				break;
			}
		}

		if (active_tab)
		{
			S32 const element_index = OrientationElement();
			F32 const value = project_mouse_onto_line(position, size, mouse_position);
			state->value = value - state->active_tab_click_offset;
			Float2 const center = active_tab->widget->layout_offset + active_tab->widget->layout_result * 0.5f;
			bool exchanged = false;

			F32 const center_value = center[element_index];
			if (active_tab->next_sibling)
			{
				TabState* const tab = active_tab->next_sibling;
				Element* const child = tab->widget;
				F32 const child_layout_offset = child->layout_offset[element_index];
				F32 const child_layout_result = child->layout_result[element_index];

				if (center_value > child_layout_offset && center_value < child_layout_offset + child_layout_result)
				{
					if (active_tab->prev_sibling != nullptr)
					{
						active_tab->prev_sibling->next_sibling = tab;
					}
					// child_layout_offset = state->value_before_active;
					tab->prev_sibling = active_tab->prev_sibling;
					if (tab->next_sibling)
					{
						tab->next_sibling->prev_sibling = active_tab;
					}
					active_tab->next_sibling = tab->next_sibling;
					active_tab->prev_sibling = tab;
					tab->next_sibling = active_tab;
					if (state->tab_list == active_tab)
					{
						state->tab_list = tab;
					}
					exchanged = true;
					// fprintf(stdout, "moving\n");
				}
			}

			if (!exchanged && active_tab->prev_sibling)
			{
				TabState* const tab = active_tab->prev_sibling;
				Element* const child = tab->widget;
				F32 const child_layout_offset = child->layout_offset[element_index];
				F32 const child_layout_result = child->layout_result[element_index];

				if (center_value > child_layout_offset && center_value < child_layout_offset + child_layout_result)
				{
					if (active_tab->next_sibling != nullptr)
					{
						active_tab->next_sibling->prev_sibling = tab;
					}
					// child_layout_offset = state->value_before_active;
					tab->next_sibling = active_tab->next_sibling;
					if (tab->prev_sibling)
					{
						tab->prev_sibling->next_sibling = active_tab;
					}
					active_tab->prev_sibling = tab->prev_sibling;
					active_tab->next_sibling = tab;
					tab->prev_sibling = active_tab;
					if (state->tab_list == tab)
					{
						state->tab_list = active_tab;
					}
					// fprintf(stdout, "moving\n");
				}
			}
			clamp_value(active_tab->widget);
		}

		if (mouse_position.x >= position.x && mouse_position.y >= position.y && mouse_position.x < position.x + layout_result.x && mouse_position.y < position.y + layout_result.y)
		{
			g_widget_context->push_hovered(owning_widget);
		}

		Element::process_mouse_move(position, mouse_position);
	}

	static constexpr F32 scrollbar_width = 5.0f;

	S32 OrientationElement() const
	{
		switch (orientation)
		{
			case TabBarOrientation::BottomToTop:
			case TabBarOrientation::TopToBottom:
			{
				return 1;
			}
			break;

			case TabBarOrientation::LeftToRight:
			case TabBarOrientation::RightToLeft:
			{
				return 0;
			}
			break;
		}
	}

	void clamp_value(Element* active_widget)
	{
		F32 size = 0.0f;
		switch (orientation)
		{
			case TabBarOrientation::BottomToTop:
			case TabBarOrientation::TopToBottom:
			{
				size = active_widget->layout_result.y / layout_result.y;
			}
			break;

			case TabBarOrientation::LeftToRight:
			case TabBarOrientation::RightToLeft:
			{
				size = active_widget->layout_result.x / layout_result.x;
			}
			break;
		}
		state->value = Clamp(state->value, 0.0f, 1.0f - size);
	}

	Float2 layout(ParentConstraints const& parent_constraints) override
	{
		S32 const element_index = OrientationElement();
		S32 const inv_element_index = 1 - element_index;

		Float2 const size = calc_size(parent_constraints);
		ParentConstraints const constraints{.min_size = Float2{}, .max_size = size};
		F32 offset = 0.0f;
		for (TabState* tab = state->tab_list; tab != nullptr; tab = tab->next_sibling)
		{
			Element* child = tab->widget;
			Float2 const child_size = child->layout(constraints);
			child->layout_result = child_size;
			if (child->owning_widget == g_active_element_owner)
			{
				child->layout_offset[element_index] = size[element_index] * state->value;
				child->layout_offset[inv_element_index] = 0.0f;
			}
			else
			{
				child->layout_offset[element_index] = offset;
				child->layout_offset[inv_element_index] = 0.0f;
			}

			offset += child_size[element_index];
		}
		return calc_size(parent_constraints);
	}

	void paint(Float2 offset, Float2 size) override
	{
		// #TODO: Enable this again - this helps things stay clamped when we are resized
		/*if (g_active_widget)
		{
			clamp_value();
		}*/
		RenderClipRect const previous_clip_rect = g_widget_context->push_clip_rect(RenderClipRect{
			.position = offset,
			.size = size,
		});
		Element* active_child = nullptr;
		for (TabState* tab = state->tab_list; tab != nullptr; tab = tab->next_sibling)
		{
			Element* child = tab->widget;
			if (child->owning_widget == g_active_element_owner)
			{
				active_child = child;
			}
			else
			{
				child->paint(offset + child->layout_offset, child->layout_result);
			}
		}
		// Draw active child on top of all other tabs
		if (active_child)
		{
			active_child->paint(offset + active_child->layout_offset, active_child->layout_result);
		}
		/*if (state->active_tab)
		{
			g_widget_context->push_render_box({
				.position = offset + Float2{size.x * state->value, 0.0f},
				.size = first_child->layout_result,
				.color = rgb(0, 0, 255),
			});
		}*/
		g_widget_context->push_clip_rect(previous_clip_rect);
	}

private:
	TabBarState* state;
	TabBarOrientation orientation;
};

static void TabBarWidget(WidgetBuildContext& build_context, TabBarDesc&& props)
{
	Element* element = g_element_tree->PushElement<TabBar>(PAW_MOVE(props), GetCurrentWidget());
	build_context.ChildrenSlot();
	g_element_tree->PopElement(element);
}

#define TAB_BAR(...) STATELESS_WIDGET(TabBarWidget, __VA_ARGS__)

struct TabProps
{
};

static void TabWidget(WidgetBuildContext& build_context, TabProps&&)
{
	Element* element = g_element_tree->PushElement<RelativeOffset>(RelativeOffsetDesc{}, GetCurrentWidget());
	build_context.ChildrenSlot();
	g_element_tree->PopElement(element);
}

#define TAB(...) STATELESS_WIDGET(TabWidget, __VA_ARGS__)

struct TestStatelessWidgetProps
{
	Float4 color{1.0f, 1.0f, 1.0f, 1.0f};
	bool show_other = false;
};

static void TestStatelessWidget2(WidgetBuildContext& build_context, TestStatelessWidgetProps&& props)
{
	BOX({.width = Size::fill(), .height = Size::fill(), .color = props.color})
	{
		PADDING({.left = 5.0f, .top = 5.0f, .right = 5.0f, .bottom = 5.0f})
		{
			FLEX_BOX({.orientation = Orientation::Horizontal, .width = Size::fill(), .height = Size::fill(), .gap = 5.0f})
			{
				FLEX_ITEM({})
				{
					BOX({.width = Size::fill(), .height = Size::fill(), .color = rgb(200, 55, 55)});
				}
				if (props.show_other)
				{
					build_context.ChildrenSlot();
				}
			}
		}
	}
}

#define TEST_STATELESS2(...) STATELESS_WIDGET(TestStatelessWidget2, __VA_ARGS__)

static void TestStatelessWidget(WidgetBuildContext& build_context, TestStatelessWidgetProps&& props)
{
	BOX({.width = Size::fill(), .height = Size::fill(), .color = props.color})
	{
		PADDING(PaddingProps::All(5.0f))
		{
			FLEX_BOX({.orientation = Orientation::Horizontal, .width = Size::fill(), .height = Size::fill(), .gap = 5.0f})
			{
				FLEX_ITEM({})
				{
					BOX({.width = Size::fill(), .height = Size::fill(), .color = rgb(200, 55, 55)});
				}
				if (props.show_other)
				{
					build_context.ChildrenSlot();
				}
				FLEX_ITEM({})
				{
					TEST_STATELESS2({.color = rgb(255, 0, 221), .show_other = props.show_other})
					{
						FLEX_ITEM({})
						{
							BOX({
								.width = Size::fill(),
								.height = Size::fill(),
								.color = rgb(55, 200, 79),
							});
						}
					}
				}
			}
		}
	}
}

#define TEST_STATELESS(...) STATELESS_WIDGET(TestStatelessWidget, __VA_ARGS__)

struct TestState
{
	int color;
};

struct TestStatefulWidgetProps
{
};

static void TestStatefulWidget(WidgetBuildContext&, TestStatefulWidgetProps&&, TestState& state)
{
	Float4 const color = state.color % 2 == 0 ? rgb(255, 0, 0) : rgb(0, 255, 0);
	state.color++;
	BOX({.width = Size::fill(), .height = Size::fill(), .color = color});
}

#define TEST_STATEFUL(...) STATEFUL_WIDGET(TestStatefulWidget, __VA_ARGS__)

static int g_frame_count = 0;
static Widget* g_root = nullptr;

// static bool g_switched = false;

template <typename PropsType>
static void TestThing(StatelessWidgetFunctionType<PropsType>* func)
{
	WidgetBuildContext context;
	func(context, {});
}

Slice<RenderItem const> UIUpdate(Float2 ui_size)
{
	// fprintf(stdout, "==================== Frame Start =================\n");

	g_last_frame_hovered_element_owner = g_foreground_widget_context.get_current_frame_hovered() != nullptr ? g_foreground_widget_context.get_current_frame_hovered() : g_background_widget_context.get_current_frame_hovered();

	g_foreground_widget_context.frame_reset(ui_size);
	g_background_widget_context.frame_reset(ui_size);
	g_widget_context = &g_background_widget_context;

	g_widget_allocator->FreeAll();
	if (g_root == nullptr)
	{
		// RootWidget* root = PAW_NEW_IN(g_widget_allocator, RootWidget)();
		g_root = new Widget();
	}
	g_current_parent = g_root;
	g_current_sibling = nullptr;

	g_frame_count++;

	g_element_tree->StartFrame();

	// TEST_STATELESS({.color = rgb(255, 251, 0), .show_other = true})
	// {
	// 	FLEX_ITEM({})
	// 	{
	// 		BOX({
	// 			.width = Size::fill(),
	// 			.height = Size::fill(),
	// 			.color = rgb(55, 55, 200),
	// 		});
	// 	}
	// }
	// FLEX_BOX({.orientation = Orientation::Horizontal, .width = Size::fill(), .height = Size::fill()})
	// {
	// 	FLEX_ITEM({.factor = 2.0f})
	// 	{
	// 		BOX({
	// 			.width = Size::fill(),
	// 			.height = Size::fill(),
	// 			.color = rgb(55, 55, 200),
	// 		});
	// 	}
	// 	FLEX_ITEM({})
	// 	{
	// 		BOX({
	// 			.width = Size::fill(),
	// 			.height = Size::fill(),
	// 			.color = rgb(200, 55, 200),
	// 		});
	// 	}
	// }

	// #if 0

	// Gdiplus::Color const background_base = g_frame_count % 2 == 0 ? Gdiplus::Color(255, 255, 0, 0) : Gdiplus::Color(255, 230, 230, 230);
	Float4 const background_base = argb(255, 230, 230, 230);
	Float4 const background_layer1 = argb(255, 248, 248, 248);
	// Float4 const background_layer2 = rgba(255, 255, 255, 255);

	static SliderState slide_state{
		.value = 15.0f,
	};

	BOX({
		.width = Size::fill(),
		.height = Size::fill(),
		.color = background_base,
	})
	{
		FLEX_BOX({
			.orientation = Orientation::Vertical,
			.width = Size::fill(),
			.height = Size::fill(),
		})
		{
			FLEX_BOX({
				.orientation = Orientation::Horizontal,
				.width = Size::fill(),
				.height = Size::pixel(80.0f),
			})
			{
				// Logo
				PADDING({10.0f, 10.0f, 10.0f, 10.0f})
				{
					BOX({
						.width = Size::pixel(80.0f),
						.height = Size::fill(),
						.color = argb(255, 120, 187, 250),
					});
				}

				FLEX_ITEM({})
				{
					FLEX_BOX({
						.orientation = Orientation::Vertical,
						.width = Size::fill(),
						.height = Size::fill(),
						.gap = 10.0f,
					})
					{
						FLEX_ITEM({0.75f})
						{
							// Menu Bar
							FLEX_BOX({
								.orientation = Orientation::Horizontal,
								.width = Size::fill(),
								.height = Size::fill(),
								.gap = 1.0f,
							})
							{
								Float4 const button_color = argb(255, 213, 213, 213);
								for (int i = 0; i < 7; i++)
								{
									Float4 menu_item_color = button_color;
									bool menu_item_hovered = false;
									bool menu_item_currently_clicked = false;
									bool menu_item_click_finished = false;
									CURSOR_INTERACT({
										.out_hovered = &menu_item_hovered,
										.out_currently_clicked = &menu_item_currently_clicked,
										.out_click_finished = &menu_item_click_finished,
									})
									{
										if (menu_item_hovered)
										{
											menu_item_color = rgb(255, 0, 0);
										}

										if (menu_item_currently_clicked)
										{
											menu_item_color = rgb(0, 255, 0);
										}

										if (menu_item_click_finished)
										{
											menu_item_color = rgb(0, 0, 255);
										}
										CLIP_RECT({})
										{
											BOX({
												.width = Size::pixel(50.0f),
												.height = Size::fill(),
												.color = menu_item_color,
											})
											{
												// if (menu_item_hovered)
												if (false)
												{
													FOREGROUND_PAINTER({})
													{
														RELATIVE_OFFSET({
															.x = Offset::pixel(0.0f),
															.y = Offset::parent_size_relative(1.0f),
														})
														{
															bool menu_hovered = false;
															CURSOR_INTERACT({.out_hovered = &menu_hovered})
															{
																if (menu_hovered || menu_item_hovered)
																{
																	BOX({
																		.width = Size::fractional(3.0f),
																		.height = Size::fractional(2.0f),
																		.color = rgb(255, 0, 0),
																	})
																	{
																	}
																}
															}
														}
													}
												}
											}
										}
									}
								}

								FLEX_ITEM({})
								{
								}

								BOX({
									.width = Size::pixel(50.0f),
									.height = Size::fill(),
									.color = button_color,
								})
								{
								}

								BOX({
									.width = Size::pixel(50.0f),
									.height = Size::fill(),
									.color = button_color,
								})
								{
								}

								BOX({
									.width = Size::pixel(50.0f),
									.height = Size::fill(),
									.color = button_color,
								})
								{
								}
							}
						}
						FLEX_ITEM({1.25f})
						{
							// Tab Bar
							/*FLEX_BOX({
								.orientation = Orientation::Horizontal,
								.width = Size::fill(),
								.height = Size::fill(),
							})
							{
								BOX({
									.width = Size::pixel(200.0f),
									.height = Size::fill(),
									.color = background_layer1,
								})
								{
								}
							}*/
							PADDING({0.0f, 0.0f, 10.0f, 0.0f})
							{
								static TabState first_tab{
									.name = "first",
									.color = rgb(200, 55, 55),
								};
								static TabState second_tab{
									.name = "second",
									.color = rgb(55, 200, 55),
									.prev_sibling = &first_tab,
								};
								static TabState third_tab{
									.name = "third",
									.color = rgb(55, 55, 200),
									.prev_sibling = &second_tab,
								};
								static TabState fourth_tab{
									.name = "fourth",
									.color = rgb(55, 200, 200),
									.prev_sibling = &third_tab,
								};
								static bool set_state = false;
								if (!set_state)
								{
									first_tab.next_sibling = &second_tab;
									second_tab.next_sibling = &third_tab;
									third_tab.next_sibling = &fourth_tab;
									set_state = true;
								}
								static TabBarState state{
									.tab_list = &first_tab,
								};

								Slice<TabState* const> tabs{&first_tab, &second_tab, &third_tab, &fourth_tab};

								TAB_BAR({
									.state = &state,
									.orientation = TabBarOrientation::LeftToRight,
								})
								{
									for (TabState* const tab : tabs)
									{
										WIDGET_ID(reinterpret_cast<PtrSize>(tab))
										{
											TAB({})
											{
												tab->widget = g_element_tree->GetCurrentElement();
												BOX({
													.width = Size::pixel(200.0f),
													.height = Size::fill(),
													.color = tab->color,
												})
												{
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}

			BOX({
				.width = Size::fill(),
				.height = Size::pixel(40.0f),
				.color = background_layer1,
			})
			{
			}

			FLEX_ITEM({})
			{
				PADDING({5.0f, 5.0f, 5.0f, 5.0f})
				{
					FLEX_BOX({
						.orientation = Orientation::Horizontal,
						.width = Size::fill(),
						.height = Size::fill(),
						.gap = 5.0f,
					})
					{
						BOX({
							.width = Size::pixel(200.0f),
							.height = Size::fill(),
							.color = background_layer1,
						})
						{
							PADDING({5.0f, 5.0f, 5.0f, 5.0f})
							{
								CLIP_RECT({})
								{
									// static ScrollViewState scroll_state{};
									SCROLL_VIEW({})
									{
										FLEX_BOX({
											.orientation = Orientation::Vertical,
											.width = Size::fill(),
											.height = Size::shrink_wrap(),
											.gap = 5.0f,
										})
										{
											for (int i = 0; i < 10; i++)
											{
												// WIDGET_ID(static_cast<PtrSize>(i))
												{
													FLEX_BOX({
														.orientation = Orientation::Vertical,
														.width = Size::fill(),
														.height = Size::pixel(100.0f),
													})
													{
														BOX({
															.width = Size::fill(),
															.height = Size::pixel(25.0f),
															.color = rgb(213, 213, 213),
														})
														{
														}

														FLEX_ITEM({})
														{
															BOX({
																.width = Size::fill(),
																.height = Size::fill(),
																.color = background_base,
															})
															{
															}
														}
													}
												}
											}
										}
									}
								}
							}
						}

						FLEX_BOX({
							.orientation = Orientation::Vertical,
							.width = Size::pixel(250.0f),
							.height = Size::fill(),
						})
						{
							BOX({
								.width = Size::fractional(0.5f),
								.height = Size::pixel(30.0f),
								.color = background_layer1,
							})
							{
							}

							FLEX_ITEM({})
							{
								BOX({
									.width = Size::fill(),
									.height = Size::fill(),
									.color = background_layer1,
								})
								{
									FLEX_BOX({
										.orientation = Orientation::Vertical,
										.width = Size::fill(),
										.height = Size::fill(),
									})
									{
										if (slide_state.value >= 0.5f)
										{
											WIDGET_ID(0)
											{
												FLEX_ITEM({})
												{
													BOX({
														.width = Size::fill(),
														.height = Size::fill(),
														.color = rgb(200, 55, 55),
													});
												}
											}

											WIDGET_ID(1)
											{
												FLEX_ITEM({})
												{
													BOX({
														.width = Size::fill(),
														.height = Size::fill(),
														.color = rgb(55, 200, 55),
													});
												}
											}
										}
										else
										{
											WIDGET_ID(1)
											{
												FLEX_ITEM({})
												{
													BOX({
														.width = Size::fill(),
														.height = Size::fill(),
														.color = rgb(55, 200, 55),
													});
												}
											}

											WIDGET_ID(0)
											{
												FLEX_ITEM({})
												{
													BOX({
														.width = Size::fill(),
														.height = Size::fill(),
														.color = rgb(200, 55, 55),
													});
												}
											}
										}
									}
								}
							}
						}

						FLEX_ITEM({})
						{
							BOX({
								.width = Size::fill(),
								.height = Size::fill(),
								.color = argb(255, 184, 199, 214),
							})
							{
								PADDING({10.0f, 10.0f, 10.0f, 10.0f})
								{

									FLEX_BOX({
										.width = Size::fill(),
										.height = Size::fill(),
										.gap = 5.0f,
									})
									{
										FLEX_ITEM({})
										{
											static TabState first_tab{
												.name = "first",
												.color = rgb(200, 55, 55),
											};
											static TabState second_tab{
												.name = "second",
												.color = rgb(55, 200, 55),
												.prev_sibling = &first_tab,
											};
											static TabState third_tab{
												.name = "third",
												.color = rgb(55, 55, 200),
												.prev_sibling = &second_tab,
											};
											static TabState fourth_tab{
												.name = "fourth",
												.color = rgb(55, 200, 200),
												.prev_sibling = &third_tab,
											};
											static bool set_state = false;
											if (!set_state)
											{
												first_tab.next_sibling = &second_tab;
												second_tab.next_sibling = &third_tab;
												third_tab.next_sibling = &fourth_tab;
												set_state = true;
											}
											static TabBarState state{
												.tab_list = &first_tab,
											};

											Slice<TabState* const> tabs{&first_tab, &second_tab, &third_tab, &fourth_tab};

											TAB_BAR({
												.state = &state,
												.orientation = TabBarOrientation::TopToBottom,
											})
											{
												for (TabState* const tab : tabs)
												{
													WIDGET_ID(reinterpret_cast<PtrSize>(tab))
													{
														TAB({})
														{
															tab->widget = g_element_tree->GetCurrentElement();
															BOX({
																.width = Size::fill(),
																.height = Size::pixel(100.0f),
																.color = tab->color,
															})
															{
															}
														}
													}
												}
											}
										}

										FLEX_ITEM({})
										{
											SLIDER({
												.width = Size::fill(),
												.height = Size::fill(),
												.state = &slide_state,
												.min_value = 0.0f,
												.max_value = 1.0f,
												.orientation = SliderOrientation::RightToLeft,
											})
											{
											}
										}

										FLEX_ITEM({})
										{
											SLIDER({
												.width = Size::fill(),
												.height = Size::fill(),
												.state = &slide_state,
												.min_value = 0.0f,
												.max_value = 1.0f,
												.orientation = SliderOrientation::TopToBottom,
											})
											{
											}
										}

										FLEX_ITEM({})
										{
											SLIDER({
												.width = Size::fill(),
												.height = Size::fill(),
												.state = &slide_state,
												.min_value = 0.0f,
												.max_value = 1.0f,
												.orientation = SliderOrientation::BottomToTop,
											})
											{
											}
										}
									}
								}
							}
						}

						FLEX_BOX({
							.orientation = Orientation::Vertical,
							.width = Size::pixel(250.0f),
							.height = Size::fill(),
							.gap = 5.0f,
						})
						{
							FLEX_ITEM({0.5f})
							{
								FLEX_BOX({
									.orientation = Orientation::Vertical,
									.width = Size::fill(),
									.height = Size::fill(),
								})
								{
									BOX({
										.width = Size::fractional(0.5f),
										.height = Size::pixel(30.0f),
										.color = background_layer1,
									})
									{
									}

									FLEX_ITEM({})
									{
										BOX({
											.width = Size::fill(),
											.height = Size::fill(),
											.color = background_layer1,
										})
										{
										}
									}
								}
							}
							FLEX_ITEM({})
							{
								FLEX_BOX({
									.orientation = Orientation::Vertical,
									.width = Size::fill(),
									.height = Size::fill(),
								})
								{
									BOX({
										.width = Size::fractional(0.5f),
										.height = Size::pixel(30.0f),
										.color = background_layer1,
									});

									FLEX_ITEM({})
									{
										BOX({
											.width = Size::fill(),
											.height = Size::fill(),
											.color = background_layer1,
										})
										{
											FLEX_BOX({.orientation = Orientation::Vertical, .width = Size::fill(), .height = Size::fill()})
											{
												FLEX_ITEM({})
												{
													TEST_STATELESS({.color = rgb(255, 251, 0), .show_other = slide_state.value > 0.5f})
													{
														FLEX_ITEM({})
														{
															BOX({
																.width = Size::fill(),
																.height = Size::fill(),
																.color = rgb(55, 55, 200),
															});
														}
													}
												}

												FLEX_ITEM({})
												{
													TEST_STATEFUL({});
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}

			BOX({
				.width = Size::fill(),
				.height = Size::pixel(40.0f),
				.color = background_layer1,
			})
			{
			}
		}
	}

	// #endif

	g_element_tree->layout(ParentConstraints{.min_size = Float2{}, .max_size = ui_size});
	// g_root->RemoveDeadChildren(g_current_frame_index);
	g_element_tree->paint(Float2{}, ui_size);

	g_current_frame_index++;

	// g_active_widget = nullptr;

	return g_background_widget_context.GetRenderItems();
}

void UITest(IAllocator* allocator)
{
	g_widget_allocator = PAW_NEW_IN(allocator, ArenaAllocator);

	g_element_tree = PAW_NEW_IN(allocator, ElementTree)();

	g_background_widget_context.init(allocator);
	g_foreground_widget_context.init(allocator);
}
