#pragma once
#include "imgui.h"

namespace ImGui
{
	// https://easyrgb.com/en/convert.php
	struct Colors
	{
		// CSS colors
		inline static ImColor AliceBlue{0.94f, 0.97f, 1.0f, 1.0f};             // ARGB: #FFF0F8FF
		inline static ImColor AntiqueWhite{0.98f, 0.92f, 0.84f, 1.0f};         // ARGB: #FFFAEBD7
		inline static ImColor Aqua{0.0f, 1.0f, 1.0f, 1.0f};                    // ARGB: #FF00FFFF
		inline static ImColor Aquamarine{0.50f, 1.0f, 0.83f, 1.0f};            // ARGB: #FF7FFFD4
		inline static ImColor Azure{0.94f, 1.0f, 1.0f, 1.0f};                  // ARGB: #FFF0FFFF
		inline static ImColor Beige{0.96f, 0.96f, 0.86f, 1.0f};                // ARGB: #FFF5F5DC
		inline static ImColor Bisque{1.0f, 0.89f, 0.77f, 1.0f};                // ARGB: #FFFFE4C4
		inline static ImColor Black{0.0f, 0.0f, 0.0f, 1.0f};                   // ARGB: #FF000000
		inline static ImColor BlanchedAlmond{1.0f, 0.92f, 0.80f, 1.0f};        // ARGB: #FFFFEBCD
		inline static ImColor Blue{0.0f, 0.0f, 1.0f, 1.0f};                    // ARGB: #FF0000FF
		inline static ImColor BlueViolet{0.54f, 0.17f, 0.89f, 1.0f};           // ARGB: #FF8A2BE2
		inline static ImColor Brown{0.65f, 0.16f, 0.16f, 1.0f};                // ARGB: #FFA52A2A
		inline static ImColor BurlyWood{0.87f, 0.72f, 0.53f, 1.0f};            // ARGB: #FFDEB887
		inline static ImColor CadetBlue{0.37f, 0.62f, 0.63f, 1.0f};            // ARGB: #FF5F9EA0
		inline static ImColor Chartreuse{0.50f, 1.0f, 0.0f, 1.0f};             // ARGB: #FF7FFF00
		inline static ImColor Chocolate{0.82f, 0.41f, 0.12f, 1.0f};            // ARGB: #FFD2691E
		inline static ImColor Coral{1.0f, 0.50f, 0.31f, 1.0f};                 // ARGB: #FFFF7F50
		inline static ImColor CornflowerBlue{0.39f, 0.58f, 0.93f, 1.0f};       // ARGB: #FF6495ED
		inline static ImColor Cornsilk{1.0f, 0.97f, 0.86f, 1.0f};              // ARGB: #FFFFF8DC
		inline static ImColor Crimson{0.86f, 0.08f, 0.24f, 1.0f};              // ARGB: #FFDC143C
		inline static ImColor Cyan{0.0f, 1.0f, 1.0f, 1.0f};                    // ARGB: #FF00FFFF
		inline static ImColor DarkBlue{0.0f, 0.0f, 0.55f, 1.0f};               // ARGB: #FF00008B
		inline static ImColor DarkCyan{0.0f, 0.55f, 0.55f, 1.0f};              // ARGB: #FF008B8B
		inline static ImColor DarkGoldenrod{0.72f, 0.53f, 0.04f, 1.0f};        // ARGB: #FFB8860B
		inline static ImColor DarkGray{0.66f, 0.66f, 0.66f, 1.0f};             // ARGB: #FFA9A9A9
		inline static ImColor DarkGreen{0.0f, 0.39f, 0.0f, 1.0f};              // ARGB: #FF006400
		inline static ImColor DarkKhaki{0.74f, 0.72f, 0.42f, 1.0f};            // ARGB: #FFBDB76B
		inline static ImColor DarkMagenta{0.55f, 0.0f, 0.55f, 1.0f};           // ARGB: #FF8B008B
		inline static ImColor DarkOliveGreen{0.33f, 0.42f, 0.18f, 1.0f};       // ARGB: #FF556B2F
		inline static ImColor DarkOrange{1.0f, 0.55f, 0.0f, 1.0f};             // ARGB: #FFFF8C00
		inline static ImColor DarkOrchid{0.60f, 0.20f, 0.80f, 1.0f};           // ARGB: #FF9932CC
		inline static ImColor DarkRed{0.55f, 0.0f, 0.0f, 1.0f};                // ARGB: #FF8B0000
		inline static ImColor DarkSalmon{0.91f, 0.59f, 0.48f, 1.0f};           // ARGB: #FFE9967A
		inline static ImColor DarkSeaGreen{0.56f, 0.74f, 0.56f, 1.0f};         // ARGB: #FF8FBC8F
		inline static ImColor DarkSlateBlue{0.28f, 0.24f, 0.55f, 1.0f};        // ARGB: #FF483D8B
		inline static ImColor DarkSlateGray{0.18f, 0.31f, 0.31f, 1.0f};        // ARGB: #FF2F4F4F
		inline static ImColor DarkTurquoise{0.0f, 0.81f, 0.82f, 1.0f};         // ARGB: #FF00CED1
		inline static ImColor DarkViolet{0.58f, 0.0f, 0.83f, 1.0f};            // ARGB: #FF9400D3
		inline static ImColor DeepPink{1.0f, 0.08f, 0.58f, 1.0f};              // ARGB: #FFFF1493
		inline static ImColor DeepSkyBlue{0.0f, 0.75f, 1.0f, 1.0f};            // ARGB: #FF00BFFF
		inline static ImColor DimGray{0.41f, 0.41f, 0.41f, 1.0f};              // ARGB: #FF696969
		inline static ImColor DodgerBlue{0.12f, 0.56f, 1.0f, 1.0f};            // ARGB: #FF1E90FF
		inline static ImColor Firebrick{0.70f, 0.13f, 0.13f, 1.0f};            // ARGB: #FFB22222
		inline static ImColor FloralWhite{1.0f, 0.98f, 0.94f, 1.0f};           // ARGB: #FFFAF0E6
		inline static ImColor ForestGreen{0.13f, 0.55f, 0.13f, 1.0f};          // ARGB: #FF228B22
		inline static ImColor Fuchsia{1.0f, 0.0f, 1.0f, 1.0f};                 // ARGB: #FFFF00FF
		inline static ImColor Gainsboro{0.86f, 0.86f, 0.86f, 1.0f};            // ARGB: #FFDCDCDC
		inline static ImColor GhostWhite{0.97f, 0.97f, 1.0f, 1.0f};            // ARGB: #FFF8F8FF
		inline static ImColor Gold{1.0f, 0.84f, 0.0f, 1.0f};                   // ARGB: #FFFFD700
		inline static ImColor Goldenrod{0.85f, 0.65f, 0.13f, 1.0f};            // ARGB: #FFDAA520
		inline static ImColor Gray{0.50f, 0.50f, 0.50f, 1.0f};                 // ARGB: #FF808080
		inline static ImColor Green{0.0f, 0.50f, 0.0f, 1.0f};                  // ARGB: #FF008000
		inline static ImColor GreenYellow{0.68f, 1.0f, 0.18f, 1.0f};           // ARGB: #FFADFF2F
		inline static ImColor Honeydew{0.94f, 1.0f, 0.94f, 1.0f};              // ARGB: #FFF0FFF0
		inline static ImColor HotPink{1.0f, 0.41f, 0.71f, 1.0f};               // ARGB: #FFFF69B4
		inline static ImColor IndianRed{0.80f, 0.36f, 0.36f, 1.0f};            // ARGB: #FFCD5C5C
		inline static ImColor Indigo{0.29f, 0.0f, 0.51f, 1.0f};                // ARGB: #FF4B0082
		inline static ImColor Ivory{1.0f, 1.0f, 0.94f, 1.0f};                  // ARGB: #FFFFFFF0
		inline static ImColor Khaki{0.94f, 0.90f, 0.55f, 1.0f};                // ARGB: #FFF0E68C
		inline static ImColor Lavender{0.90f, 0.90f, 0.98f, 1.0f};             // ARGB: #FFE6E6FA
		inline static ImColor LavenderBlush{1.0f, 0.94f, 0.96f, 1.0f};         // ARGB: #FFFFF0F5
		inline static ImColor LawnGreen{0.49f, 0.99f, 0.0f, 1.0f};             // ARGB: #FF7CFC00
		inline static ImColor LemonChiffon{1.0f, 0.98f, 0.80f, 1.0f};          // ARGB: #FFFFFACD
		inline static ImColor LightBlue{0.68f, 0.85f, 0.90f, 1.0f};            // ARGB: #FFADD8E6
		inline static ImColor LightCoral{0.94f, 0.50f, 0.50f, 1.0f};           // ARGB: #FFF08080
		inline static ImColor LightCyan{0.88f, 1.0f, 1.0f, 1.0f};              // ARGB: #FFE0FFFF
		inline static ImColor LightGoldenrodYellow{0.98f, 0.98f, 0.82f, 1.0f}; // ARGB: #FFFAFAD2
		inline static ImColor LightGray{0.83f, 0.83f, 0.83f, 1.0f};            // ARGB: #FFD3D3D3
		inline static ImColor LightGreen{0.56f, 0.93f, 0.56f, 1.0f};           // ARGB: #FF90EE90
		inline static ImColor LightPink{1.0f, 0.71f, 0.76f, 1.0f};             // ARGB: #FFFFB6C1
		inline static ImColor LightSalmon{1.0f, 0.63f, 0.48f, 1.0f};           // ARGB: #FFFFA07A
		inline static ImColor LightSeaGreen{0.13f, 0.70f, 0.67f, 1.0f};        // ARGB: #FF20B2AA
		inline static ImColor LightSkyBlue{0.53f, 0.81f, 0.98f, 1.0f};         // ARGB: #FF87CEFA
		inline static ImColor LightSlateGray{0.47f, 0.53f, 0.60f, 1.0f};       // ARGB: #FF778899
		inline static ImColor LightSteelBlue{0.69f, 0.77f, 0.87f, 1.0f};       // ARGB: #FFB0C4DE
		inline static ImColor LightYellow{1.0f, 1.0f, 0.88f, 1.0f};            // ARGB: #FFFFFFE0
		inline static ImColor Lime{0.0f, 1.0f, 0.0f, 1.0f};                    // ARGB: #FF00FF00
		inline static ImColor LimeGreen{0.20f, 0.80f, 0.20f, 1.0f};            // ARGB: #FF32CD32
		inline static ImColor Linen{0.98f, 0.94f, 0.90f, 1.0f};                // ARGB: #FFFAF0E6
		inline static ImColor Magenta{1.0f, 0.0f, 1.0f, 1.0f};                 // ARGB: #FFFF00FF
		inline static ImColor Maroon{0.50f, 0.0f, 0.0f, 1.0f};                 // ARGB: #FF800000
		inline static ImColor MediumAquamarine{0.40f, 0.80f, 0.67f, 1.0f};     // ARGB: #FF66CDAA
		inline static ImColor MediumBlue{0.0f, 0.0f, 0.80f, 1.0f};             // ARGB: #FF0000CD
		inline static ImColor MediumOrchid{0.73f, 0.33f, 0.83f, 1.0f};         // ARGB: #FFBA55D3
		inline static ImColor MediumPurple{0.58f, 0.44f, 0.86f, 1.0f};         // ARGB: #FF9370DB
		inline static ImColor MediumSeaGreen{0.24f, 0.70f, 0.44f, 1.0f};       // ARGB: #FF3CB371
		inline static ImColor MediumSlateBlue{0.48f, 0.41f, 0.93f, 1.0f};      // ARGB: #FF7B68EE
		inline static ImColor MediumSpringGreen{0.0f, 0.98f, 0.60f, 1.0f};     // ARGB: #FF00FA9A
		inline static ImColor MediumTurquoise{0.28f, 0.82f, 0.80f, 1.0f};      // ARGB: #FF48D1CC
		inline static ImColor MediumVioletRed{0.78f, 0.08f, 0.52f, 1.0f};      // ARGB: #FFC71585
		inline static ImColor MidnightBlue{0.10f, 0.10f, 0.44f, 1.0f};         // ARGB: #FF191970
		inline static ImColor MintCream{0.96f, 1.0f, 0.98f, 1.0f};             // ARGB: #FFF5FFFA
		inline static ImColor MistyRose{1.0f, 0.89f, 0.88f, 1.0f};             // ARGB: #FFFFE4E1
		inline static ImColor Moccasin{1.0f, 0.89f, 0.71f, 1.0f};              // ARGB: #FFFFE4B5
		inline static ImColor NavajoWhite{1.0f, 0.87f, 0.68f, 1.0f};           // ARGB: #FFFFDEAD
		inline static ImColor Navy{0.0f, 0.0f, 0.50f, 1.0f};                   // ARGB: #FF000080
		inline static ImColor OldLace{0.99f, 0.96f, 0.90f, 1.0f};              // ARGB: #FFFDF5E6
		inline static ImColor Olive{0.50f, 0.50f, 0.0f, 1.0f};                 // ARGB: #FF808000
		inline static ImColor OliveDrab{0.42f, 0.56f, 0.14f, 1.0f};            // ARGB: #FF6B8E23
		inline static ImColor Orange{1.0f, 0.65f, 0.0f, 1.0f};                 // ARGB: #FFFFA500
		inline static ImColor OrangeRed{1.0f, 0.27f, 0.0f, 1.0f};              // ARGB: #FFFF4500
		inline static ImColor Orchid{0.85f, 0.44f, 0.84f, 1.0f};               // ARGB: #FFDA70D6
		inline static ImColor PaleGoldenrod{0.93f, 0.91f, 0.67f, 1.0f};        // ARGB: #FFEEE8AA
		inline static ImColor PaleGreen{0.60f, 0.98f, 0.60f, 1.0f};            // ARGB: #FF98FB98
		inline static ImColor PaleTurquoise{0.69f, 0.93f, 0.93f, 1.0f};        // ARGB: #FFAFEEEE
		inline static ImColor PaleVioletRed{0.86f, 0.44f, 0.58f, 1.0f};        // ARGB: #FFDB7093
		inline static ImColor PapayaWhip{1.0f, 0.94f, 0.84f, 1.0f};            // ARGB: #FFFFEFD5
		inline static ImColor PeachPuff{1.0f, 0.85f, 0.73f, 1.0f};             // ARGB: #FFFFDAB9
		inline static ImColor Peru{0.80f, 0.52f, 0.25f, 1.0f};                 // ARGB: #FFCD853F
		inline static ImColor Pink{1.0f, 0.75f, 0.80f, 1.0f};                  // ARGB: #FFFFC0CB
		inline static ImColor Plum{0.87f, 0.63f, 0.87f, 1.0f};                 // ARGB: #FFDDA0DD
		inline static ImColor PowderBlue{0.69f, 0.88f, 0.90f, 1.0f};           // ARGB: #FFB0E0E6
		inline static ImColor Purple{0.50f, 0.0f, 0.50f, 1.0f};                // ARGB: #FF800080
		inline static ImColor Red{1.0f, 0.0f, 0.0f, 1.0f};                     // ARGB: #FFFF0000
		inline static ImColor RosyBrown{0.74f, 0.56f, 0.56f, 1.0f};            // ARGB: #FFBC8F8F
		inline static ImColor RoyalBlue{0.25f, 0.41f, 0.88f, 1.0f};            // ARGB: #FF4169E1
		inline static ImColor SaddleBrown{0.55f, 0.27f, 0.07f, 1.0f};          // ARGB: #FF8B4513
		inline static ImColor Salmon{0.98f, 0.50f, 0.45f, 1.0f};               // ARGB: #FFFA8072
		inline static ImColor SandyBrown{0.96f, 0.64f, 0.38f, 1.0f};           // ARGB: #FFF4A460
		inline static ImColor SeaGreen{0.18f, 0.55f, 0.34f, 1.0f};             // ARGB: #FF2E8B57
		inline static ImColor SeaShell{1.0f, 0.96f, 0.93f, 1.0f};              // ARGB: #FFFFF5EE
		inline static ImColor Sienna{0.63f, 0.32f, 0.18f, 1.0f};               // ARGB: #FFA0522D
		inline static ImColor Silver{0.75f, 0.75f, 0.75f, 1.0f};               // ARGB: #FFC0C0C0
		inline static ImColor SkyBlue{0.53f, 0.81f, 0.92f, 1.0f};              // ARGB: #FF87CEEB
		inline static ImColor SlateBlue{0.42f, 0.35f, 0.80f, 1.0f};            // ARGB: #FF6A5ACD
		inline static ImColor SlateGray{0.44f, 0.50f, 0.56f, 1.0f};            // ARGB: #FF708090
		inline static ImColor Snow{1.0f, 0.98f, 0.98f, 1.0f};                  // ARGB: #FFFFFAFA
		inline static ImColor SpringGreen{0.0f, 1.0f, 0.50f, 1.0f};            // ARGB: #FF00FF7F
		inline static ImColor SteelBlue{0.27f, 0.51f, 0.71f, 1.0f};            // ARGB: #FF4682B4
		inline static ImColor Tan{0.82f, 0.71f, 0.55f, 1.0f};                  // ARGB: #FFD2B48C
		inline static ImColor Teal{0.0f, 0.50f, 0.50f, 1.0f};                  // ARGB: #FF008080
		inline static ImColor Thistle{0.85f, 0.75f, 0.85f, 1.0f};              // ARGB: #FFD8BFD8
		inline static ImColor Tomato{1.0f, 0.39f, 0.28f, 1.0f};                // ARGB: #FFFF6347
		inline static ImColor Transparent{1.0f, 1.0f, 1.0f, 0.0f};             // ARGB: #00FFFFFF
		inline static ImColor Turquoise{0.25f, 0.88f, 0.82f, 1.0f};            // ARGB: #FF40E0D0
		inline static ImColor Violet{0.93f, 0.51f, 0.93f, 1.0f};               // ARGB: #FFEE82EE
		inline static ImColor Wheat{0.96f, 0.87f, 0.70f, 1.0f};                // ARGB: #FFF5DEB3
		inline static ImColor White{1.0f, 1.0f, 1.0f, 1.0f};                   // ARGB: #FFFFFFFF
		inline static ImColor WhiteSmoke{0.96f, 0.96f, 0.96f, 1.0f};           // ARGB: #FFF5F5F5
		inline static ImColor Yellow{1.0f, 1.0f, 0.0f, 1.0f};                  // ARGB: #FFFFFF00
		inline static ImColor YellowGreen{0.60f, 0.80f, 0.20f, 1.0f};          // ARGB: #FF9ACD32

		// others
		inline static ImColor Freemode{0.17647f, 0.43137f, 0.72549, 1.0f};     // ARGB: #FF2D6EB9
		inline static ImColor IngameBg{0.0f, 0.0f, 0.0f, 0.54901f};            // ARGB: #8C000000
		inline static ImColor DisabledText{0.0f, 0.0f, 0.0f, 0.8f};            // ARGB: #CC000000
	};
}