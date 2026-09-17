-- Run with: aseprite -b --script tests/fixtures/make.lua
-- Produces the fixture art next to this script. Index 0 is the transparent index,
-- and its alpha of 0 is what makes Aseprite write the modern palette chunk, which
-- is the only one LDtk reads.
local dir = "tests/fixtures/art/"

local function palette_file()
    local spr = Sprite(1, 1, ColorMode.INDEXED)
    local pal = Palette(8)

    pal:setColor(0, Color{r=0, g=0, b=0, a=0})
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

    pal:setColor(0, Color{r=0, g=0, b=0, a=0})
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

local function tiles_file()
    local spr = Sprite(32, 16, ColorMode.INDEXED)
    local pal = Palette(8)

    pal:setColor(0, Color{r=0, g=0, b=0, a=0})
    pal:setColor(1, Color{r=32, g=32, b=64})
    pal:setColor(2, Color{r=255, g=0, b=0})
    pal:setColor(3, Color{r=0, g=255, b=0})
    pal:setColor(4, Color{r=0, g=0, b=255})
    pal:setColor(5, Color{r=255, g=255, b=255})
    pal:setColor(6, Color{r=255, g=255, b=0})
    pal:setColor(7, Color{r=128, g=128, b=128})
    spr:setPalette(pal)
    spr.layers[1].name = "tiles"

    local img = spr.cels[1].image
    for k = 0, 7 do
        local ox, oy = (k % 4) * 8, (k // 4) * 8
        local index = k < 7 and k + 1 or 5
        for y = 0, 7 do for x = 0, 7 do img:putPixel(ox + x, oy + y, index) end end
        if k == 7 then img:putPixel(ox, oy, 6) end
    end

    spr:saveAs("tests/fixtures/levels/tiles.aseprite")
    spr:close()
end

-- A font that is not a readable face: each cell inks known columns, so every
-- expected rect and advance in test_text is a hand-checkable number.
local function font_cells(spr, cell_w, cell_h, columns)
    local img = spr.cels[1].image

    img:clear(0)

    for n = 1, 95 do
        local ox, oy = (n % columns) * cell_w, (n // columns) * cell_h
        local inked = {[0] = {3}, [1] = {0, 2}, [2] = {0, 1, 2, 3}, [3] = {0}}

        for _, cx in ipairs(inked[n % 4]) do
            for y = 0, cell_h - 1 do img:putPixel(ox + cx, oy + y, 5) end
        end
    end
end

local function font_file(path, w, h, cell_w, cell_h, columns, grid, frames)
    local spr = Sprite(w, h, ColorMode.INDEXED)
    local pal = Palette(8)

    pal:setColor(0, Color{r=0, g=0, b=0, a=0})
    pal:setColor(1, Color{r=32, g=32, b=64})
    pal:setColor(2, Color{r=255, g=0, b=0})
    pal:setColor(3, Color{r=0, g=255, b=0})
    pal:setColor(4, Color{r=0, g=0, b=255})
    pal:setColor(5, Color{r=255, g=255, b=255})
    pal:setColor(6, Color{r=255, g=255, b=0})
    pal:setColor(7, Color{r=128, g=128, b=128})
    spr:setPalette(pal)
    spr.gridBounds = grid

    if columns then font_cells(spr, cell_w, cell_h, columns) end
    if frames == 2 then spr:newFrame() end

    spr:saveAs(path)
    spr:close()
end

local function font_files()
    font_file("tests/fixtures/fonts/body.aseprite", 64, 36, 4, 6, 16, Rectangle(0, 0, 4, 6), 1)

    local bad = "tests/fixtures/fonts-bad/"

    font_file(bad .. "uneven/body.aseprite", 100, 48, 4, 6, 16, Rectangle(0, 0, 12, 8), 1)
    font_file(bad .. "origin/body.aseprite", 64, 36, 4, 6, 16, Rectangle(1, 0, 4, 6), 1)
    font_file(bad .. "wide/body.aseprite", 260, 6, 260, 6, 1, Rectangle(0, 0, 260, 6), 1)
    font_file(bad .. "few/body.aseprite", 32, 12, 4, 6, 8, Rectangle(0, 0, 4, 6), 1)
    font_file(bad .. "frames/body.aseprite", 64, 36, 4, 6, 16, Rectangle(0, 0, 4, 6), 2)
    font_file(bad .. "blank/body.aseprite", 64, 36, 4, 6, 16, Rectangle(0, 0, 4, 6), 1)

    local spr = app.open(bad .. "blank/body.aseprite")
    local img = spr.cels[1].image

    for y = 0, 5 do for x = 0, 3 do img:putPixel(4 + x, y, 0) end end

    spr:saveAs(bad .. "blank/body.aseprite")
    spr:close()
end

palette_file()
player_file()
tiles_file()
font_files()
