using System;
using System.Collections.Generic;
using System.Globalization;

namespace Hades.Scripting
{
    public struct Vector3
    {
        public float X;
        public float Y;
        public float Z;

        public Vector3(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
        }
    }

    public sealed class EntityContext
    {
        public uint EntityId { get; }
        public string Name { get; }
        public Vector3 Position { get; set; }

        public EntityContext(uint entityId, string name, Vector3 position)
        {
            EntityId = entityId;
            Name = name;
            Position = position;
        }
    }

    public abstract class HadesScript
    {
        public virtual void OnStart(EntityContext context) { }
        public virtual void OnUpdate(EntityContext context, float deltaTime) { }
        public virtual void OnKeyDown(EntityContext context, int keyCode) { }
        public virtual void OnKeyUp(EntityContext context, int keyCode) { }
    }

    public static class HadesAPI
    {
        private static readonly Dictionary<string, string> _observed = new();

        public static void Observe(string key, int value) => _observed[key] = value.ToString(CultureInfo.InvariantCulture);
        public static void Observe(string key, float value) => _observed[key] = value.ToString(CultureInfo.InvariantCulture);
        public static void Observe(string key, double value) => _observed[key] = value.ToString(CultureInfo.InvariantCulture);
        public static void Observe(string key, bool value) => _observed[key] = value ? "true" : "false";
        public static void Observe(string key, string value) => _observed[key] = "\"" + EscapeJson(value ?? "") + "\"";

        public static void Clear() => _observed.Clear();

        internal static string SerializeJson()
        {
            if (_observed.Count == 0) return "{}";
            var sb = new System.Text.StringBuilder("{");
            bool first = true;
            foreach (var kvp in _observed)
            {
                if (!first) sb.Append(',');
                sb.Append('"').Append(EscapeJson(kvp.Key)).Append("\":");
                sb.Append(kvp.Value);
                first = false;
            }
            sb.Append('}');
            return sb.ToString();
        }

        private static string EscapeJson(string s)
        {
            return s.Replace("\\", "\\\\").Replace("\"", "\\\"")
                    .Replace("\n", "\\n").Replace("\r", "\\r").Replace("\t", "\\t");
        }
    }
}
