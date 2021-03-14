reset
set xlabel "sequence"
set ylabel "time(ns)"
set title "fibnacci performance"
set yrange[1:1000]
set term png enhanced font 'Verdana 10'
set output "time_elapsed.png"


plot 'scripts/utime.txt' using 1:2 with linespoints title "user", \
'scripts/ktime.txt' using 1:2 with linespoints title "kernel", \
"<paste scripts/utime.txt scripts/ktime.txt" using 1:($2 - $4) with lp title "syscall"
