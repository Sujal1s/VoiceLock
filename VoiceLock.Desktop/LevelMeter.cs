using System.Windows;
using System.Windows.Media;

namespace VoiceLock.Desktop;

public sealed class LevelMeter : FrameworkElement
{
    public static readonly DependencyProperty ValueProperty = DependencyProperty.Register(
        nameof(Value), typeof(double), typeof(LevelMeter),
        new FrameworkPropertyMetadata(0d, FrameworkPropertyMetadataOptions.AffectsRender));
    public double Value { get => (double)GetValue(ValueProperty); set => SetValue(ValueProperty,value); }

    protected override void OnRender(DrawingContext context)
    {
        base.OnRender(context);
        double level=double.IsFinite(Value)?Math.Clamp(Value,0,1):0;
        int lit=(int)Math.Ceiling(level*10);
        for(int i=0;i<10;i++)
        {
            var color=i<lit
                ? System.Windows.Media.Color.FromRgb((byte)(98+13*i),(byte)(221+2*i),(byte)(180-10*i))
                : System.Windows.Media.Color.FromRgb(41,49,46);
            var rectangle=new Rect(0,ActualHeight-(i+1)*ActualHeight/10,ActualWidth,Math.Max(0,ActualHeight/10-3));
            context.DrawRoundedRectangle(new SolidColorBrush(color),null,rectangle,3,3);
        }
    }
}
