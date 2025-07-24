namespace data_schema
{
	[AttributeUsage(AttributeTargets.Class)]
	public class Primitive : Attribute
	{

	}

    [Primitive]
    public class Float3
    {
        public float X { get; set; }
		public float Y {get; set; }
		public float Z {get; set; }
        
    }

	[AttributeUsage(AttributeTargets.Property)]
    public class StreamingRef : Attribute
    {

    }

    public class Game
    {
        [StreamingRef]
        public Map MainMap { get; set; }
    }

    public class Map
    {
        [StreamingRef]
        public List<StreamingTile> Tiles {get; set;}
    }

    public class StreamingTile
    {
        public Float3 Position { get; set; }
        public List<Entity> Entities { get; set; }

    }

    public class Entity
    {
        public List<Component> Components { get; set; }
    }

    public class Component
    {

    }

    public class TransformComponent : Component
    {
		public Float3 Position { get; set; }

        public List<TransformComponent> Children { get; set; }
	}
}
