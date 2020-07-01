#!/usr/bin/env python3
# Generate a tiled layout in a simple and stupid way
# v20200701IST0240, HanishKVC
#
import pygame as pg

w=1920
h=1080
s=pg.surface.Surface((w,h))
stW=32
stW=4
stH=32
stH=4
x = 0
y = 0
iCur = 0
colors=[
	(200,0,0),
	(0,200,0),
	(0,0,200),
	(200,200,200),
	(200,200,0),
	(0,200,200),
	(200,0,200),
	(0,0,0)]
iC = 0
while iCur < (w*h):
	if (iCur%(stW*stH)) == 0:
		color = colors[iC]
		iC = (iC+1)%len(colors)
	s.set_at((x,y),color)
	x += 1
	if (x >= w):
		x = 0
		y += 1
	iCur += 1

pg.image.save(s,"/tmp/ssti.png")
