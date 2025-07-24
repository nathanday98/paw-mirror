#include <testing/testing.h>

#include <core/assert.h>
#include <core/slice.inl>

#include <cstdlib>
#include <new>
#include <cstdio>
#include <cstring>

#include "ecs.h"
#include "ecs.generated.h"

static S32 u64_safe_subtract(U64 lhs, U64 rhs)
{
	bool const lhs_is_bigger = lhs > rhs;
	U64 const biggest = lhs_is_bigger ? lhs : rhs;
	U64 const smallest = lhs_is_bigger ? rhs : lhs;
	S32 result = static_cast<S32>(biggest - smallest);
	result *= lhs_is_bigger ? 1 : -1;
	return result;
}

template <typename A, typename B>
static S32 pointer_diff_Bytes(A* lhs, B* rhs)
{
	U64 const x = reinterpret_cast<U64>(lhs);
	U64 const y = reinterpret_cast<U64>(rhs);
	return u64_safe_subtract(x, y);
}

PAW_TEST(u64_safe_subtract)
{
	PAW_TEST_EXPECT_EQUAL(u64_safe_subtract(1000, 500), 500);
	PAW_TEST_EXPECT_EQUAL(u64_safe_subtract(500, 1000), -500);
	PAW_TEST_EXPECT_EQUAL(u64_safe_subtract(0, 1000), -1000);
	PAW_TEST_EXPECT_EQUAL(u64_safe_subtract(UINT64_MAX - 1, UINT64_MAX), -1);
	PAW_TEST_EXPECT_EQUAL(u64_safe_subtract(UINT64_MAX, UINT64_MAX - 1), 1);
}

class EntityComponent : NonCopyable
{
public:
	virtual void init()
	{
	}

	virtual ~EntityComponent()
	{
	}
};

class Entity;

template <typename T>
class RelativeSlice
{
public:
	T& operator[](S32 index)
	{
		PAW_ASSERT(index >= 0 && index < count, "index is not in range");
		T* items = calc_items_ptr();
		return items[index];
	}

	T& operator[](S32 index) const
	{
		PAW_ASSERT(index >= 0 && index < count, "index is not in range");
		T* items = calc_items_ptr();
		return items[index];
	}

	PtrSize CalcTotalSizeBytes() const
	{
		return sizeof(T) * count;
	}

	T* calc_items_ptr()
	{
		Byte* const this_Byte_ptr = reinterpret_cast<Byte*>(this);
		Byte* const items_Byte_ptr = this_Byte_ptr + start_offset_Bytes;
		return reinterpret_cast<T*>(items_Byte_ptr);
	}

	T const* calc_items_ptr() const
	{
		Byte const* const this_Byte_ptr = reinterpret_cast<Byte const*>(this);
		Byte const* const items_Byte_ptr = this_Byte_ptr + start_offset_Bytes;
		return reinterpret_cast<T const*>(items_Byte_ptr);
	}

	S32 start_offset_Bytes; // offset from this ptr
	S32 count;
};

class EntitySystem : NonCopyable
{
};

struct EntityComponentRef
{
	S32 offset_Bytes; // Relative to this ptr
	EntityComponentType type;

	void init(Byte* component_ptr, EntityComponentType in_type)
	{
		offset_Bytes = pointer_diff_Bytes(component_ptr, this);
		type = in_type;
	}

	template <typename T>
	T* get_ptr()
	{
		PAW_ASSERT(T::type == type, "type does not match, did you pass in the right template parameter?");
		Byte* this_Bytes = reinterpret_cast<Byte*>(this);
		return reinterpret_cast<T*>(this_Bytes + offset_Bytes);
	}

	Byte* get_Byte_ptr()
	{
		Byte* this_Bytes = reinterpret_cast<Byte*>(this);
		return this_Bytes + offset_Bytes;
	}

	EntityComponent* get_base_ptr()
	{
		Byte* this_Bytes = reinterpret_cast<Byte*>(this);
		return reinterpret_cast<EntityComponent*>(this_Bytes + offset_Bytes);
	}
};

struct EntitySystemRef
{
	S32 offset_Bytes; // Relative to this ptr
	EntitySystemType type;

	void init(Byte* system_ptr, EntitySystemType in_type)
	{
		offset_Bytes = pointer_diff_Bytes(system_ptr, this);
		type = in_type;
	}

	template <typename T>
	T* get_ptr()
	{
		PAW_ASSERT(T::type == type, "type does not match, did you pass in the right template parameter?");
		Byte* this_Bytes = reinterpret_cast<Byte*>(this);
		return reinterpret_cast<T*>(this_Bytes + offset_Bytes);
	}

	Byte* get_Byte_ptr()
	{
		Byte* this_Bytes = reinterpret_cast<Byte*>(this);
		return this_Bytes + offset_Bytes;
	}

	EntitySystem* get_base_ptr()
	{
		Byte* this_Bytes = reinterpret_cast<Byte*>(this);
		return reinterpret_cast<EntitySystem*>(this_Bytes + offset_Bytes);
	}
};

class Entity
{
public:
	RelativeSlice<EntityComponentRef> component_refs;
	RelativeSlice<EntitySystemRef> system_refs;

	PtrSize components_start_offset_Bytes;
	PtrSize components_total_size_Bytes;

	PtrSize systems_start_offset_Bytes;
	PtrSize systems_total_size_Bytes;
};

class WorldSystem
{
};

class World
{
};

class StaticMeshComponent : public EntityComponent
{
public:
	virtual void init() override
	{
		printf("init static mesh %s\n", name);
	}

	static constexpr EntityComponentType type = EntityComponentType::StaticMesh;

	char const* name;
};

class DynamicMeshComponent : public EntityComponent
{
public:
	virtual void init() override
	{
		printf("init dynamic mesh %s\n", name);
	}

	static constexpr EntityComponentType type = EntityComponentType::DynamicMesh;

	char const* name;
	S32 a;
	S32 b;
};

class TestSystem : public EntitySystem
{
public:
	void frame_start()
	{
		printf("I exist at %llu\n", reinterpret_cast<U64>(this));

		for (int i = 0; i < static_meshes.count; i++)
		{
			StaticMeshComponent& static_mesh = static_meshes[i];
			printf("%s\n", static_mesh.name);
		}

		for (int i = 0; i < dynamic_meshes.count; i++)
		{
			DynamicMeshComponent& dynamic_mesh = dynamic_meshes[i];
			printf("%s\n", dynamic_mesh.name);
		}
	}

	static constexpr EntitySystemType type = EntitySystemType::Test;

	RelativeSlice<StaticMeshComponent> static_meshes;
	RelativeSlice<DynamicMeshComponent> dynamic_meshes;
	int a;
	int b;
};

static constexpr PtrSize g_entity_component_sizes[EntityComponentType_value_count] = {
	sizeof(StaticMeshComponent),
	sizeof(DynamicMeshComponent),
};

static constexpr PtrSize g_entity_system_sizes[EntitySystemType_value_count] = {
	sizeof(TestSystem),
};

static void init_component(EntityComponentType type, Byte* component_ptr)
{
	switch (type)
	{
		case EntityComponentType::StaticMesh:
		{
			reinterpret_cast<StaticMeshComponent*>(component_ptr)->init();
		}
		break;

		case EntityComponentType::DynamicMesh:
		{
			reinterpret_cast<DynamicMeshComponent*>(component_ptr)->init();
		}
		break;
	}
}

PAW_TEST(entity_system)
{

	// World world{};

	Slice<EntityComponentType const> entity_components = ConstSliceFromInitList({
		EntityComponentType::StaticMesh,
		EntityComponentType::StaticMesh,
		EntityComponentType::DynamicMesh,
	});

	Slice<EntitySystemType const> entity_systems = ConstSliceFromInitList({
		EntitySystemType::Test,
	});

	PtrSize components_size_Bytes = 0;
	for (EntityComponentType type : entity_components)
	{
		components_size_Bytes += g_entity_component_sizes[S32(type)];
	}

	PtrSize systems_size_Bytes = 0;
	for (EntitySystemType type : entity_systems)
	{
		systems_size_Bytes += g_entity_system_sizes[S32(type)];
	}

	PtrSize const components_refs_size_Bytes = sizeof(EntityComponentRef) * entity_components.count;
	PtrSize const systems_refs_size_Bytes = sizeof(EntitySystemRef) * entity_systems.count;
	PtrSize const entity_size = sizeof(Entity) + components_size_Bytes + systems_size_Bytes + components_refs_size_Bytes + systems_refs_size_Bytes;

	Byte* entity_memory = reinterpret_cast<Byte*>(std::malloc(entity_size));
	{
		Entity* entity = new (entity_memory) Entity();
		entity->component_refs.count = entity_components.count;
		entity->system_refs.count = entity_systems.count;

		Byte* const component_refs_start_ptr = entity_memory + sizeof(Entity);
		Byte* const system_refs_start_ptr = component_refs_start_ptr + entity->component_refs.CalcTotalSizeBytes();
		Byte* const components_start_ptr = system_refs_start_ptr + entity->system_refs.CalcTotalSizeBytes();
		Byte* const systems_start_ptr = components_start_ptr + components_size_Bytes;

		entity->component_refs.start_offset_Bytes = pointer_diff_Bytes(component_refs_start_ptr, &entity->component_refs);
		entity->system_refs.start_offset_Bytes = pointer_diff_Bytes(system_refs_start_ptr, &entity->system_refs);

		entity->components_start_offset_Bytes = pointer_diff_Bytes(components_start_ptr, entity_memory);
		entity->components_total_size_Bytes = components_size_Bytes;
		entity->systems_start_offset_Bytes = pointer_diff_Bytes(systems_start_ptr, entity_memory);
		entity->systems_total_size_Bytes = systems_size_Bytes;

		Byte* entity_memory_ptr = components_start_ptr;

		for (S32 i = 0; i < entity->component_refs.count; i++)
		{
			EntityComponentType const type = entity_components[i];
			EntityComponentRef& ref = entity->component_refs[i];
			ref.init(entity_memory_ptr, type);
			entity_memory_ptr += g_entity_component_sizes[S32(type)];
		}

		entity_memory_ptr = systems_start_ptr;

		for (S32 i = 0; i < entity->system_refs.count; i++)
		{
			EntitySystemType const type = entity_systems[i];
			EntitySystemRef& ref = entity->system_refs[i];
			ref.init(entity_memory_ptr, type);
			entity_memory_ptr += g_entity_system_sizes[S32(type)];
		}

		TestSystem* const test_system = new (entity->system_refs[0].get_Byte_ptr()) TestSystem();

		entity_memory_ptr = entity_memory + entity->components_start_offset_Bytes;

		StaticMeshComponent* const first_mesh = new (entity->component_refs[0].get_Byte_ptr()) StaticMeshComponent();
		StaticMeshComponent* const second_mesh = new (entity->component_refs[1].get_Byte_ptr()) StaticMeshComponent();

		test_system->static_meshes.start_offset_Bytes = pointer_diff_Bytes(entity->component_refs[0].get_Byte_ptr(), &test_system->static_meshes);
		test_system->static_meshes.count = 2;

		DynamicMeshComponent* const third_mesh = new (entity->component_refs[2].get_Byte_ptr()) DynamicMeshComponent();

		test_system->dynamic_meshes.start_offset_Bytes = pointer_diff_Bytes(entity->component_refs[2].get_Byte_ptr(), &test_system->dynamic_meshes);
		test_system->dynamic_meshes.count = 1;

		first_mesh->name = "First Static Mesh";
		second_mesh->name = "Second Static Mesh";
		third_mesh->name = "First Dynamic Mesh";
		third_mesh->a = 100;
		third_mesh->b = 44;

		test_system->frame_start();
		test_system->a = 100;
		test_system->b = 44;
	}

	Byte* entity_memory_moved = reinterpret_cast<Byte*>(std::malloc(entity_size));
	std::memmove(entity_memory_moved, entity_memory, entity_size);
	// std::memset(entity_memory, 0, entity_size);

	Entity* entity = reinterpret_cast<Entity*>(entity_memory_moved);

	for (S32 i = 0; i < entity->component_refs.count; i++)
	{
		EntityComponentRef& ref = entity->component_refs[i];
		// Byte* component_ptr = ref.get_Byte_ptr();
		// init_component(ref.type, component_ptr);
		EntityComponent* const component = ref.get_base_ptr();
		component->init();
	}

	TestSystem* const test_system = entity->system_refs[0].get_ptr<TestSystem>();
	test_system->frame_start();
}

int main(int arg_count, char* args[])
{
	int result = test_main(arg_count, args);
	return result;
}
