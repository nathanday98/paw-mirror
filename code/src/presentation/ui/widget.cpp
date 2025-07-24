#include "widget.h"

#include "core/std.h"
#include <core/math.h>
#include <core/assert.h>
#include <core/reflection.h>

Size Size::pixel(F32 value)
{
	return {.type = Size::Type::Pixel, .value = value};
}

Size Size::fractional(F32 value)
{
	return {.type = Size::Type::Fractional, .value = value};
}

Size Size::shrink_wrap()
{
	return {.type = Size::Type::ShrinkWrap, .value = 0.0f};
}

Size Size::fill()
{
	return fractional(1.0f);
}

Offset Offset::pixel(F32 value)
{
	return {.type = Offset::Type::Pixel, .value = value};
}

Offset Offset::parent_size_relative(F32 value)
{
	return {.type = Offset::Type::ParentSizeRelative, .value = value};
}

Offset Offset::child_size_relative(F32 value)
{
	return {.type = Offset::Type::ChildSizeRelative, .value = value};
}

Element::Element(Size width /*= Size::shrink_wrap()*/, Size height /*= Size::shrink_wrap()*/)
	: width(width)
	, height(height)
{
}

ClassInfo const& Element::GetStaticTypeInfo()
{
	static ClassInfo class_info{nullptr};
	return class_info;
}

ClassInfo const& Element::GetTypeInfo() const
{
	return GetStaticTypeInfo();
}

Float2 Element::calc_size(ParentConstraints const& parent_constraints) const
{
	Float2 const parent_size = parent_constraints.max_size;
	Float2 result{};
	switch (width.type)
	{
		case Size::Type::Pixel:
			result.x = width.value;
			break;

		case Size::Type::Fractional:
			result.x = width.value * parent_size.x;
			break;

		case Size::Type::ShrinkWrap:
			result.x = 0.0f;
			break;
	}

	switch (height.type)
	{
		case Size::Type::Pixel:
			result.y = height.value;
			break;

		case Size::Type::Fractional:
			result.y = height.value * parent_size.y;
			break;

		case Size::Type::ShrinkWrap:
			result.y = 0.0f;
			break;
	}
	return result;
}

Float2 Element::layout(ParentConstraints const& parent_constraints)
{
	Float2 size = calc_size(parent_constraints);
	if (width.type == Size::Type::ShrinkWrap)
	{
		size.x = parent_constraints.max_size.x;
	}

	if (height.type == Size::Type::ShrinkWrap)
	{
		size.y = parent_constraints.max_size.y;
	}

	Float2 max_child_size{
		-g_f32_max,
		-g_f32_max,
	};
	for (Element* child = first_child; child != nullptr; child = child->sibling)
	{
		Float2 const child_size = child->layout(
			ParentConstraints{
				.min_size = parent_constraints.min_size,
				.max_size = size,
			});
		child->layout_result = child_size;
		max_child_size.x = Max(child_size.x, max_child_size.x);
		max_child_size.y = Max(child_size.y, max_child_size.y);
	}

	Float2 result = size;

	if (width.type == Size::Type::ShrinkWrap)
	{
		result.x = max_child_size.x;
	}

	if (height.type == Size::Type::ShrinkWrap)
	{
		result.y = max_child_size.y;
	}

	return result;
}

void Element::paint(Float2 offset, Float2 /*size*/)
{
	for (Element* child = first_child; child != nullptr; child = child->sibling)
	{
		child->paint(offset + child->layout_offset, child->layout_result);
	}
}

void Element::process_mouse_wheel(Float2 wheel_delta)
{
	for (Element* child = first_child; child != nullptr; child = child->sibling)
	{
		child->process_mouse_wheel(wheel_delta);
	}
}

Element* Element::hit_test(Float2 offset, MouseButton button, Float2 mouse_position)
{
	Float2 const position = offset + layout_offset;

	for (Element* child = first_child; child != nullptr; child = child->sibling)
	{
		Element* const result = child->hit_test(position, button, mouse_position);
		if (result != nullptr)
		{
			return result;
		}
	}

	if (mouse_position.x >= position.x && mouse_position.y >= position.y && mouse_position.x < position.x + layout_result.x && mouse_position.y < position.y + layout_result.y)
	{
		return this;
	}
	return nullptr;
}

void Element::process_mouse_button_up(MouseButton button)
{
	for (Element* child = first_child; child != nullptr; child = child->sibling)
	{
		child->process_mouse_button_up(button);
	}
}

void Element::process_mouse_move(Float2 offset, Float2 mouse_position)
{
	for (Element* child = first_child; child != nullptr; child = child->sibling)
	{
		child->process_mouse_move(offset + layout_offset, mouse_position);
	}
}

void Element::on_mouse_button_down(MouseButton /* button */, Float2 /* mouse_position */)
{
}

// void Element::RemoveDeadChildren(PtrSize current_frame_index)
// {
// 	Element* prev_child = nullptr;
// 	for (Element* child = first_child; child != nullptr; child = child->sibling)
// 	{
// 		if (child->last_frame_index != current_frame_index)
// 		{
// 			if (prev_child)
// 			{
// 				prev_child->sibling = child->sibling;
// 			}
// 			else
// 			{
// 				first_child = nullptr;
// 			}
// 			children_count--;

// 			// #TODO: Maybe delete all the of its children too?
// 			// delete static_cast<WidgetType*>(child);
// 			// fprintf(stdout, "delete widget\n");
// 		}
// 		else
// 		{
// 			child->RemoveDeadChildren(current_frame_index);
// 			prev_child = child;
// 		}
// 	}
// }
