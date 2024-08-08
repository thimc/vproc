# vproc
a graphical version of ps(1) for 9front

![vproc](vproc.png)

## Keyboard shortcuts
q / Del to quit

Up / Down to scroll one line

Page Up / Down to scroll 10 lines

Home / End to scroll to the top or bottom

## Description

	vproc [ -ahinr ] [ -d seconds ] [ -s sortfmt ]

Vproc is a graphical tool that displays information about
processes, it scans the /proc file system every 5 seconds.

The *-a* flag causes vproc to also display the arguments
passed to each process.

The *-d* flag specifies the sleep interval between each poll.

The *-h* flag prints a usage messsage to standard error.

The *-i* flag causes vproc to sort the list in a reversed
order.

The *-n* flag causes vproc to also display the note group of
each process.

The *-r* flag causes vproc to display the elapsed real time
for each process.

The *-s* flag determines how the list should be sorted.  If
the flag is not specified it will default to **p**, which is
the same behaviour as ps(1).  The sortfmt is expected to be
one or more of the following:

- *p* sort by the PID
- *U* sort by username
- *u* sort by user time
- *s* sort by system time
- *r* sort by real time
- *m* sort by memory usage
- *S* sort by the process state
- *c* sort by command name

## Bugs
Yes.

## Features missing
[Most likely](https://github.com/thimc/vproc/blob/main/TODO).
