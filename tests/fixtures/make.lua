-- Run with: aseprite -b --script tests/fixtures/make.lua
-- Produces the fixture art next to this script.
local dir = "tests/fixtures/"

local function palette_file()
    local spr = Sprite(1, 1, ColorMode.INDEXED)
    local pal = Palette(8)
    pal:setColor(0, Color{r=0, g=0, b=0})
    pal:setColor(1, Color{r=32, g=32, b=64})
    pal:setColor(2, Color{r=255, g=0, b=0})
    pal:setColor(3, Color{r=0, g=255, b=0})
    pal:setColor(4, Color{r=0, g=0, b=255})
    pal:setColor(5, Color{r=255, g=255, b=255})
    pal:setColor(6, Color{r=255, g=255, b=0})
    pal:setColor(7, Color{r=128, g=128, b=128})
    spr:setPalette(pal)
    spr:saveAs(dir .. "palette.aseprite")
    spr:close()
end

local function player_file()
    local spr = Sprite(16, 16, ColorMode.INDEXED)
    local pal = Palette(4)
    pal:setColor(0, Color{r=0, g=0, b=0})
    pal:setColor(1, Color{r=255, g=0, b=0})
    pal:setColor(2, Color{r=0, g=255, b=0})
    pal:setColor(3, Color{r=0, g=0, b=255})
    spr:setPalette(pal)
    spr.layers[1].name = "body"
    local img = spr.cels[1].image
    for y = 4, 11 do for x = 4, 11 do img:putPixel(x, y, 1) end end
    spr.frames[1].duration = 0.1
    local frame2 = spr:newFrame()
    frame2.duration = 0.2
    local img2 = spr.cels[2].image
    img2:clear(0)
    for y = 4, 11 do for x = 6, 13 do img2:putPixel(x, y, 2) end end
    spr:newTag(1, 2).name = "walk"
    spr:saveAs(dir .. "player.aseprite")
    spr:close()
end

palette_file()
player_file()
