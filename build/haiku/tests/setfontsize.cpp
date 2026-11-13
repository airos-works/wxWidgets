// Sets the size of the system fonts (plain, bold, fixed) the way the Fonts
// preferences do, to test the port at other scales: air/OS scales its UI by
// the plain font size (12 = 100%).
//   setfontsize 18     (150%)
// Not a wx program: build with
//   $CROSS-g++ --sysroot=$SYSROOT setfontsize.cpp -o setfontsize -lbe
#include <Application.h>
#include <Font.h>

#include <stdio.h>
#include <stdlib.h>

// private, src/kits/interface/Font.cpp
extern void _set_system_font_(const char *which, font_family family,
                              font_style style, float size);

int main(int argc, char **argv)
{
    if ( argc != 2 )
    {
        fprintf(stderr, "usage: %s SIZE\n", argv[0]);
        return 1;
    }

    const float size = atof(argv[1]);

    BApplication app("application/x-vnd.wx-setfontsize");

    static const struct
    {
        const char *which;
        const BFont *font;
        float factor;
    } fonts[] =
    {
        { "plain", be_plain_font, 1 },
        { "bold", be_bold_font, 1 },
        { "fixed", be_fixed_font, 1 },
    };

    for ( unsigned n = 0; n < sizeof(fonts) / sizeof(fonts[0]); n++ )
    {
        font_family family;
        font_style style;
        fonts[n].font->GetFamilyAndStyle(&family, &style);
        _set_system_font_(fonts[n].which, family, style, size * fonts[n].factor);
        printf("%s: %s %s %g -> %g\n", fonts[n].which, family, style,
               fonts[n].font->Size(), size * fonts[n].factor);
    }

    return 0;
}
