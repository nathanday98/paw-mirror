#pragma once

#include <core/std.h>
#include <core/math_types.h>
#include <core/reflected_type.h>
#include <core/reflection_types.h>
#include <core/src_location_types.h>

#include "widget_types.h"

enum class EventState
{
	Propagate,
	Handled,
};

enum class MouseButton
{
	Left,
	Middle,
	Right,
};

static constexpr PtrSize g_null_widget_id = 0xFFFFFFFFFFFFFFFF;

struct Widget
{
	Widget* parent = nullptr;
	Widget* sibling = nullptr;
	Widget* first_child = nullptr;
	S32 children_count = 0;
	PtrSize id = g_null_widget_id;
	PtrSize last_frame_index = 0;
	SrcLocation last_src_loc;
	PtrSize type;
	void* state = nullptr;
	char const* debug_type_name = "unknown";
};

PAW_REFLECT_CLASS()
class Element : public ReflectedClass
{
public:
	Element(Size width = Size::shrink_wrap(), Size height = Size::shrink_wrap());

	virtual ~Element()
	{
	}

	static ClassInfo const& GetStaticTypeInfo();
	virtual ClassInfo const& GetTypeInfo() const;

	virtual Float2 layout(ParentConstraints const& parent_constraints);
	virtual void paint(Float2 offset, Float2 size);

	virtual void process_mouse_wheel(Float2 wheel_delta);
	Element* hit_test(Float2 offset, MouseButton button, Float2 mouse_position);
	virtual void process_mouse_button_up(MouseButton button);
	virtual void process_mouse_move(Float2 offset, Float2 mouse_position);

	virtual void on_mouse_button_down(MouseButton button, Float2 mouse_position);

	Float2 calc_size(ParentConstraints const& parent_constraints) const;

	Element* parent = nullptr;
	Element* sibling = nullptr;
	Element* first_child = nullptr;
	Widget* owning_widget = nullptr;
	S32 children_count = 0;

	Float2 layout_result{};
	Float2 layout_offset{};

protected:
	Size width;
	Size height;
};
