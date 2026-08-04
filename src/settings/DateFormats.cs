using System.Text;

namespace WorldClockSettings;

// Mirrors the C++ gadget's FormatDatePattern token logic exactly, so the
// settings preview matches what the gadget will draw. English month/weekday
// names are hard-coded (not culture-dependent) to match the C++ side.
public static class DateFormats
{
    private static readonly string[] WdShort = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    private static readonly string[] WdLong  = { "Sunday", "Monday", "Tuesday", "Wednesday",
                                                 "Thursday", "Friday", "Saturday" };
    private static readonly string[] WdJp    = { "日", "月", "火", "水", "木", "金", "土" };
    private static readonly string[] MoShort = { "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    private static readonly string[] MoLong  = { "", "January", "February", "March", "April", "May",
                                                 "June", "July", "August", "September", "October",
                                                 "November", "December" };

    public sealed record Preset(string Pattern, string Description)
    {
        public override string ToString() => Pattern;
    }

    // Offered in the picker; the user can also type any custom pattern.
    public static readonly Preset[] Presets =
    {
        new("ddd, MMM d",       "Fri, Jun 12"),
        new("dddd, MMMM d",     "Friday, June 12"),
        new("yyyy-MM-dd",       "2026-06-12"),
        new("MM/dd/yyyy",       "06/12/2026"),
        new("dd/MM/yyyy",       "12/06/2026"),
        new("M/d",              "6/12"),
        new("d MMM yyyy",       "12 Jun 2026"),
        new("yyyy年M月d日",      "2026年6月12日"),
        new("M月d日(aaa)",       "6月12日(金)"),
        new("yyyy年M月d日 aaaa", "2026年6月12日 金曜日"),
    };

    public static string Format(string pattern, DateTime d)
    {
        if (string.IsNullOrEmpty(pattern)) return "";
        int wd = (int)d.DayOfWeek; // Sunday = 0
        var sb = new StringBuilder();
        int i = 0, n = pattern.Length;
        int Run(char ch) { int j = i, c = 0; while (j < n && pattern[j] == ch) { c++; j++; } return c; }

        while (i < n)
        {
            char ch = pattern[i];
            switch (ch)
            {
                case 'y':
                {
                    int r = Run('y');
                    sb.Append(r >= 4 ? d.Year.ToString("D4") : (d.Year % 100).ToString("D2"));
                    i += r; break;
                }
                case 'M':
                {
                    int r = Run('M');
                    if (r >= 4)      sb.Append(MoLong[d.Month]);
                    else if (r == 3) sb.Append(MoShort[d.Month]);
                    else             sb.Append(r == 2 ? d.Month.ToString("D2") : d.Month.ToString());
                    i += r; break;
                }
                case 'd':
                {
                    int r = Run('d');
                    if (r >= 4)      sb.Append(WdLong[wd]);
                    else if (r == 3) sb.Append(WdShort[wd]);
                    else             sb.Append(r == 2 ? d.Day.ToString("D2") : d.Day.ToString());
                    i += r; break;
                }
                case 'a':
                {
                    int r = Run('a');
                    sb.Append(WdJp[wd]);
                    if (r >= 4) sb.Append("曜日");
                    i += r; break;
                }
                default:
                    sb.Append(ch); i++; break;
            }
        }
        return sb.ToString();
    }
}
