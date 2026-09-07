-- Run with: aseprite -b --script examples/demo/art/make.lua
-- Produces the demo art next to this script. The palette is black, white,
-- and red.

local dir = "examples/demo/art/"

local BLACK    = Color{r=0,   g=0,   b=0}
local BG       = Color{r=18,  g=18,  b=18}   -- background
local RED      = Color{r=153, g=40,  b=42}   -- cloak
local DARK_RED = Color{r=97,  g=38,  b=39}   -- cloak shadow
local WHITE    = Color{r=249, g=249, b=248}
local GRAY     = Color{r=161, g=161, b=160}
local DIM      = Color{r=128, g=128, b=126}
local SHADOW   = Color{r=48,  g=46,  b=46}

local function palette_file()
    local spr = Sprite(1, 1, ColorMode.INDEXED)
    local pal = Palette(8)

    pal:setColor(0, BLACK)
    pal:setColor(1, BG)
    pal:setColor(2, RED)
    pal:setColor(3, WHITE)
    pal:setColor(4, DARK_RED)
    pal:setColor(5, GRAY)
    pal:setColor(6, DIM)
    pal:setColor(7, SHADOW)
    spr:setPalette(pal)
    spr:saveAs(dir .. "palette.aseprite")
    spr:close()
end

local function player_file()
    local spr = Sprite(16, 16, ColorMode.INDEXED)
    local pal = Palette(4)

    pal:setColor(0, BLACK)
    pal:setColor(1, RED)
    pal:setColor(2, WHITE)
    pal:setColor(3, DARK_RED)
    spr:setPalette(pal)
    spr.layers[1].name = "body"

    local img = spr.cels[1].image
    for y = 4, 11 do for x = 4, 11 do img:putPixel(x, y, 2) end end

    spr.frames[1].duration = 0.1

    local frame2 = spr:newFrame()
    frame2.duration = 0.2

    local img2 = spr.cels[2].image
    img2:clear(0)

    for y = 4, 11 do for x = 4, 11 do img2:putPixel(x, y, 2) end end
    spr:newTag(1, 2).name = "walk"
    spr:saveAs(dir .. "player.aseprite")
    spr:close()
end

palette_file()
player_file()
