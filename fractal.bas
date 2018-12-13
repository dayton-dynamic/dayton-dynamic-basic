
100 rem Fractal example. This is buggy.
110 cx = 0: cy = 0: r = 10: grid = 0.025: pan = 2/3: fudge = 1.000001
120 cls
130 gosub 200: rem show grid
140 print [l r u d z o c s b q   ];
150 print [Left Right Up Down Zoom Out Center Smaller Bigger Quit  ];
160 line input a$
170 cmd = instr(1, [lrudzocsbq], a$)
180 on cmd gosub 510, 520, 530, 540, 550, 560, 570, 580, 590, 600
190 goto 120

200 REM print out grid
210 FOR Y = cy - r to cy + fudge * r step 2 * grid * r
220 FOR x = cx - r to cx + fudge * r step grid * r
230 GOSUB 300: REM compute Z
240 GOSUB 400: REM map Z to something printable
250 PRINT P$;
260 NEXT X
270 PRINT chr$(27) + [[m]
280 NEXT Y
290 return

300 rem Mandelbrot computation, but I botched it.
310 rem Repeatedly square a complex number, then add x + yi
320 max = 100
330 Z = 0: X' = 0: Y' = 0
340 X' = X' ^ 2 + Y' ^ 2 + X: rem corrupts X' for next line
350 Y' = 2 * X' * Y' + Y
360 IF X' ^ 2 + Y' ^ 2 > 10000 THEN RETURN
370 Z = Z + 1
380 IF Z > 94 THEN RETURN
390 GOTO 340

400 REM map Z onto some printable p$
410 color = z mod 7
420 p$ = chr$(27) + [[1;] + str$(30+color) + [m] + chr$(32 + z)
430 RETURN

500 rem user commands
510 cx = cx + pan * r: return: rem l(eft)
520 cx = cx - pan * r: return: rem r(ight)
530 cy = cy + pan * r: return: rem u(p)
540 cy = cy - pan * r: return: rem d(own)
550 r = r / 2: return: rem z(oom in)
560 r = r * 2: return: rem (zoom )o(ut)
570 run 110: rem c(enter)
580 grid = grid * 11 / 10: return: rem s(maller)
590 grid = grid * 10 / 11: return: rem b(igger)
600 end: rem q(uit)

