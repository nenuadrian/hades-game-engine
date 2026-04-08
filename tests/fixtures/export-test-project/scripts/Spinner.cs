using Hades.Scripting;

public sealed class Spinner : HadesScript
{
    public float Speed = 90.0f;

    public override void OnUpdate(float deltaTime)
    {
        var pos = GetPosition();
        // Simple oscillation on the Y axis.
        pos.Y += MathF.Sin(Time * Speed * MathF.PI / 180.0f) * deltaTime;
        SetPosition(pos);
    }
}
