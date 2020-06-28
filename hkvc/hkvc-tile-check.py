#!/usr/bin/env python3
# Check tile patterns
# v20200629IST0111, HanishKVC

import matplotlib.pyplot as plt

#check=[ [8, 4, 0], [16,8,-4], [32,-8,4] ]
check=[[4, 0, 4], [8, 4,-4], [16,-4,4], [32,4,-12]]
plt.plot([0,50,100,150,200],[0,50,100,150,200])
x,y = 0,0
me = 0
for i in range(1024):
    if i == 0:
        plt.text(0,0,me)
        me += 1
        continue
    for k in range(len(check)):
        if (i%check[-1-k][0]) == 0:
            x += check[-1-k][1]
            y += check[-1-k][2]
            if (x >= 200):
                x = 0
                y += 16
            print(check[-1-k][0],x,y,me)
            plt.text(x,y,me)
            me += 1
            break
plt.show()

