using data_schema;
using System.Reflection;

namespace data_compiler
{
	internal class Program
	{
		static void Main(string[] args)
		{
			Game game = new Game
			{
				MainMap = new Map
				{
					Tiles = new List<StreamingTile>
					{
						new StreamingTile
						{
							Position = new Float3 { X = 10.0f, Y = 0.0f, Z = 0.0f, },
							Entities = new List<Entity>
							{
								new Entity
								{
									Components = new List<Component>
									{
										new TransformComponent
										{
											Position = new Float3 { X = 0.0f, Y = 1.0f, Z = 2.0f, },
											Children = new List<TransformComponent>
											{
												new TransformComponent
												{
													Position = new Float3 { X = 4.0f, Y = 0.0f, Z = 0.0f, },
												}
											}
										}
									}
								}
							}
						}
					}
				}
			};

			List<object> reachable_roots = new List<object>();
			reachable_roots.Add(game);

			Stack<object> object_stack = new Stack<object>();
			object_stack.Push(game);

			while(object_stack.Count > 0)
			{
				object obj = object_stack.Pop();
				Type type = obj.GetType();
				foreach(PropertyInfo property in type.GetProperties())
				{
					if (property.GetCustomAttribute<StreamingRef>() != null)
					{
						Type property_type = property.PropertyType;
						if(property_type.IsGenericType && property_type.GetGenericTypeDefinition() == typeof(List<>))
						{

						}
						object? ref_object = property.GetValue(obj);
						if (ref_object != null)
						{
							reachable_roots.Add(ref_object);
							object_stack.Push(ref_object);
						}
					}
					else
					{
						if (property.GetCustomAttribute<Primitive>() == null && !property.PropertyType.IsPrimitive)
						{
							object? ref_object = property.GetValue(obj);
							if (ref_object != null)
							{
								object_stack.Push(ref_object);
							}
						}
					}
				}
			}

			Console.WriteLine("Hello, World!");
		}
	}
}
