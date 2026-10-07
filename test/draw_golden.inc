// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Golden draw lists, written by test_draw with MAUL_UI_UPDATE_GOLDEN=1.

{"boxes",
 "header surface=7 size=200,100 scale=1\n"
 "box clip=0 rect=0,0,200,100 radii=8,8,8,8 fill=1,0,0,1 gradient=0 borders=1,4,3,2 border=0,0,1,1 border=0.214041144,0.214041144,0.214041144,1 border=0.214041144,0.214041144,0.214041144,1 border=0,0,1,1 transform=0\n"
 "box clip=0 rect=62,11,60,20 radii=10,8,6,8 fill=1,0,0,1 gradient=0 borders=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 transform=0\n"
},
{"snap-1x",
 "header surface=7 size=100,40 scale=1\n"
 "box clip=0 rect=10,0,21,10 radii=0,0,0,0 fill=0.214041144,0.214041144,0.214041144,1 gradient=0 borders=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 transform=0\n"
 "box clip=0 rect=31,0,1,10 radii=0,0,0,0 fill=0.214041144,0.214041144,0.214041144,1 gradient=0 borders=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 transform=0\n"
 "box clip=0 rect=31,0,7,10 radii=0,0,0,0 fill=0.214041144,0.214041144,0.214041144,1 gradient=0 borders=1,2,0,1 border=0,0,0,1 border=0,0,0,1 border=0,0,0,0 border=0,0,0,1 transform=0\n"
},
{"snap-2x",
 "header surface=7 size=100,40 scale=2\n"
 "box clip=0 rect=10.5,0,20,10 radii=0,0,0,0 fill=0.214041144,0.214041144,0.214041144,1 gradient=0 borders=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 transform=0\n"
 "box clip=0 rect=30.5,0,0.5,10 radii=0,0,0,0 fill=0.214041144,0.214041144,0.214041144,1 gradient=0 borders=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 transform=0\n"
 "box clip=0 rect=31,0,7,10 radii=0,0,0,0 fill=0.214041144,0.214041144,0.214041144,1 gradient=0 borders=1,2.5,0,0.5 border=0,0,0,1 border=0,0,0,1 border=0,0,0,0 border=0,0,0,1 transform=0\n"
},
{"clips-shadows-images-gradients",
 "header surface=7 size=300,200 scale=1\n"
 "clip 1 parent=0 invert=0 transform=0 rect=0,0,300,200 radii=8,8,8,8\n"
 "clip 2 parent=1 invert=0 transform=0 rect=22,22,100,80 radii=0,0,0,0\n"
 "gradient 1 kind=1 interpolation=1 angle=90 at=0 color=1,0,0,1 at=1 color=0,0,1,1\n"
 "shadow clip=0 inset=0 rect=0,0,300,200 radii=8,8,8,8 color=0,0,0,0.5 offset=0,4 blur=8 spread=1 transform=0\n"
 "box clip=0 rect=0,0,300,200 radii=8,8,8,8 fill=1,0,0,1 gradient=0 borders=2,2,2,2 border=0,0,1,1 border=0.214041144,0.214041144,0.214041144,1 border=0.214041144,0.214041144,0.214041144,1 border=0,0,1,1 transform=0\n"
 "shadow clip=0 inset=1 rect=2,2,296,196 radii=6,6,6,6 color=0,0,0,0.25 offset=1,1 blur=2 spread=0 transform=0\n"
 "box clip=1 rect=22,22,100,80 radii=0,0,0,0 fill=0,0,0,0 gradient=1 borders=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 transform=0\n"
 "image clip=1 key=42 rect=22,22,100,80 uv=0,0,1,1 slice=2,6,3,4 tint=0.214041144,0.214041144,0.214041144,1 transform=0\n"
 "box clip=2 rect=22,22,30,30 radii=0,0,0,0 fill=0.214041144,0.214041144,0.214041144,1 gradient=0 borders=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 transform=0\n"
},
{"opacity",
 "header surface=7 size=100,100 scale=1\n"
 "box clip=0 rect=0,0,100,100 radii=0,0,0,0 fill=0.0535102859,0.0535102859,0.0535102859,0.25 gradient=0 borders=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 transform=0\n"
 "box clip=0 rect=0,0,10,10 radii=0,0,0,0 fill=0.5,0,0,0.5 gradient=0 borders=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 transform=0\n"
},
{"right-to-left",
 "header surface=7 size=100,50 scale=1\n"
 "box clip=0 rect=0,0,100,50 radii=8,12,8,8 fill=1,0,0,1 gradient=0 borders=0,1,0,3 border=0,0,0,0 border=0,0,1,1 border=0,0,0,0 border=0.214041144,0.214041144,0.214041144,1 transform=0\n"
 "image clip=0 key=9 rect=0,0,100,50 uv=0,0,1,1 slice=3,2,4,1 tint=1,1,1,1 transform=0\n"
},
{"local-scale",
 "header surface=7 size=200,100 scale=1\n"
 "clip 1 parent=0 invert=0 transform=2 rect=100,0,50,50 radii=0,0,0,0\n"
 "transform 1 0.5 0 0 0.5 25 10\n"
 "transform 2 2 0 0 2 -100 0\n"
 "transform 3 2 0 0 2 -100 -20\n"
 "box clip=0 rect=0,0,100,40 radii=0,0,0,0 fill=1,0,0,1 gradient=0 borders=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 transform=1\n"
 "box clip=0 rect=0,0,20,10 radii=0,0,0,0 fill=1,0,0,1 gradient=0 borders=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 transform=1\n"
 "box clip=0 rect=100,0,50,50 radii=0,0,0,0 fill=1,0,0,1 gradient=0 borders=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 transform=2\n"
 "box clip=1 rect=100,0,50,60 radii=0,0,0,0 fill=1,0,0,1 gradient=0 borders=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 border=0,0,0,0 transform=3\n"
},
