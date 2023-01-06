#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <keyboard.h>

typedef struct Process Process;

struct Process
{
	int pid, siz;
	long u, s;
	char user[1024];
	char cmd[1024];
	char state[1024];
};
Process* proc;

enum
{
	Headers = 8,
	Scrollwidth = 13,
	Hpadding = 16,
	Vpadding = 6,
	Hstep = 70,
	Ptop = 25,
	Pstep = 21,
	Theight = 20,
};

enum 
{
	Theader = 0,
	Tview = 1,
};

Image* toolbg;
Image* toolfg;
Image* viewbg;
Image* viewfg;
Image* selbg;
Image* selfg;
Image* scrollbg;
Image* scrollfg;
Rectangle toolr;
Rectangle viewr;
Rectangle scrollr;
Rectangle scrposr;

int nprocs;
int visible;
int offset;
int reverseflag;
Dir* dir;
char* headers[] = { "pid", "user", "utime", "systime",  "size", "state", "command", nil };


void
drawtext(Image* screen, int Ttype, Point pt, char* text)
{
	Image* fg = viewfg;
	if(Ttype == Theader)
		fg = toolfg;
	Point p = Pt(screen->r.min.x + Hpadding + pt.x, screen->r.min.y + Vpadding + pt.y);
	string(screen, p, fg, ZP, display->defaultfont, text);
}

int
drawprocfield(Image* screen, int px, int py, char* fmt, ...)
{
	char buf[1024];
	int n;
	if((n = snprint(buf, sizeof(buf), "")) < 0)
		sysfatal("snprint: %r");
	va_list ap;
	va_start(ap, fmt);
	vseprint(buf+n, buf+sizeof(buf), fmt, ap);
	va_end(ap);

	if(px+screen->r.min.x+Hpadding+stringwidth(display->defaultfont, buf) >= screen->r.max.x)
		return 1000000;
	drawtext(screen, Tview, Pt(px, py), buf);

	return Hstep;
}

void
loadtheme(void)
{
	// TODO: allow theming
	toolbg = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DPalebluegreen);
	toolfg = display->black;
	viewbg = display->white;
	viewfg = display->black;
	selbg  = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0xEFEFFFEE);
	selfg  = display->black;
	scrollbg = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0x999999FF);
	scrollfg = display->white;
}

void
redraw(Image* screen)
{
	int h = 0, i, y, px, py=Ptop;
	toolr = Rect(screen->r.min.x, screen->r.min.y, screen->r.max.x, screen->r.min.y + Vpadding + Theight);
	viewr = Rect(screen->r.min.x, screen->r.min.y + Vpadding + Pstep, screen->r.max.x, screen->r.max.y);
	visible = (screen->r.max.y-screen->r.min.y-Ptop)/Pstep;
	draw(screen, toolr, toolbg, nil, ZP);
	draw(screen, viewr, viewbg, nil, ZP);

	/* print toolbar headers */
	for(i=0; i<Headers; i++){
		if (h+Hpadding+screen->r.min.x+
				stringwidth(display->defaultfont, headers[i]) > screen->r.max.x)
			break;
		drawtext(screen, Theader, Pt(h, 0), headers[i]);
		h += Hstep;
	}
	draw(screen, scrollr, scrollbg, nil, ZP);

	/* toolbar seperator */
	line(screen,
	     Pt(screen->r.min.x, screen->r.min.y + Vpadding + Theight),
	     Pt(screen->r.max.x, screen->r.min.y + Vpadding + Theight),
	     Endsquare, Endsquare, 0, display->black, ZP);

	for(i=offset; i<nprocs; i++){
		if(py + screen->r.min.y + Ptop > screen->r.max.y)
			break;
		px = 0;
		px += drawprocfield(screen, px, py, "%d", proc[i].pid);
		px += drawprocfield(screen, px, py, "%s", proc[i].user);
		px += drawprocfield(screen, px, py, "%lud:%.2lud", proc[i].u/60, proc[i].u%60);
		px += drawprocfield(screen, px, py, "%lud:%.2lud", proc[i].s/60, proc[i].s%60);
		px += drawprocfield(screen, px, py, "%dK", proc[i].siz);
		px += drawprocfield(screen, px, py, "%s", proc[i].state);
		px += drawprocfield(screen, px, py, "%s", proc[i].cmd);
		py += Pstep;
	}

	/* scrollbar */
	scrollr = screen->r;
	scrollr.min.y = screen->r.min.y + Vpadding + Theight + 1;
	scrollr.max.x = scrollr.min.x + Scrollwidth;
	if(nprocs>visible){
		h = ((double)visible/nprocs)*Dy(scrollr);
		y = ((double)offset/nprocs)*Dy(scrollr);
		scrposr = Rect(scrollr.min.x, scrollr.min.y+y, scrollr.max.x-1, scrollr.min.y+y+h);
	}else{
		scrposr = Rect(scrollr.min.x, scrollr.min.y, scrollr.max.x-1, scrollr.max.y);
	}
	draw(screen, scrposr, scrollfg, nil, ZP);

	flushimage(display, 1);
}

int
cmp(void *va, void *vb)
{
	Process a = *(Process*)va;
	Process b = *(Process*)vb;
	if(reverseflag)
		return b.pid - a.pid;
	return a.pid - b.pid;
}

void
loaddir(void)
{
	int i, fd, sfd, ac, n, p, ndirs;
	Dir* dir;
	char* buf;
	Dir* procd;
	char status[4096];
	char* av[16];

	if(proc)
		free(proc);

	if(chdir("/proc")==-1)
		sysfatal("chdir: %r");

	fd=open(".", OREAD);
	if(fd<0)
		sysfatal("open: %r");

	ndirs = dirreadall(fd, &dir);
	if(ndirs <= 0)
		sysfatal("dirreadall: %r");

	proc = malloc(ndirs*sizeof(Process));
	if(proc == nil)
		sysfatal("malloc: %r");

	nprocs=0;
	for(i=0; i<ndirs; i++) {
		procd = dir++;
		if(strcmp(procd->name,"trace")==0) {
			continue;
		}
		p = atoi(procd->name);
		buf = malloc(sizeof(char) * 50);
		if(buf == nil)
			sysfatal("malloc: %r");
		sprint(buf, "/proc/%d/status", p);
		sfd = open(buf, OREAD);
		if(sfd<0)
			continue;
		n = read(sfd, status, sizeof(status) - 1);
		if(n<=0)
			continue;
		status[n]='\0';
		if((ac = tokenize(status, av, nelem(av)-1)) < 12)
			continue;
		av[ac]=0;

		strcpy(proc[nprocs].cmd, av[0]);
		strcpy(proc[nprocs].user, av[1]);
		strcpy(proc[nprocs].state, av[2]);
		proc[nprocs].pid = p;
		proc[nprocs].u = strtoul(av[3], 0, 0)/1000;
		proc[nprocs].s = strtoul(av[4], 0, 0)/1000;
		proc[nprocs].siz = atoi(av[9]);
		free(buf);
		close(sfd);
		close(n);
		nprocs++;
	}
	close(fd);
	qsort(proc, nprocs, sizeof(Process), cmp);
}

void
eresized(int new)
{
	if(new && getwindow(display, Refnone)<0)
		sysfatal("getwindow: %r");
	redraw(screen);
}

void
scroll(int direction)
{
	offset += direction;
	if(offset+visible > nprocs)
		offset = nprocs-visible;
	if(offset < 0)
		offset = 0;
}

void
scrollmouse(Mouse* m)
{
	/* TODO: grab scrposr, compare with mouse.xy and adjust accordingly */
}

void
usage(void)
{
	fprint(2, "usage: %s [-h] [-d seconds] [-i]\n", argv0);
	exits("usage");
}

void
main(int argc, char* argv[])
{
	int e, timer, delay=5;
	Event ev;
	reverseflag=0;

	ARGBEGIN{
	case 'd':
		delay=atoi(EARGF(usage()));
		break;
	case 'h':
		usage();
		break;
	case 'i':
		reverseflag++;
		break;
	}ARGEND;

	if(initdraw(nil, nil, "vproc")<0)
		sysfatal("initdraw: %r");
	einit(Emouse|Ekeyboard);
	offset=0;
	loadtheme();
	loaddir();
	eresized(0);
	redraw(screen);
	timer = etimer(0, delay*1000);
	for(;;){
		e = event(&ev);
		if (e == Ekeyboard){
			switch(ev.kbdc){
				case Kdel: /* fallthrough */
				case 'q':
					goto end;
					break;
				case Kdown:
					scroll(1);
					goto tick;
				case Kup:
					scroll(-1);
					goto tick;
				case Kpgdown:
					scroll(10);
					goto tick;
				case Kpgup:
					scroll(-10);
					goto tick;

			}
		} else if (e == Emouse && ptinrect(ev.mouse.xy, scrollr)){
			switch(ev.mouse.buttons){
				case 1:
					scroll(-5);
					goto tick;
				case 4:
					scroll(5);
					goto tick;
				case 2:
					scrollmouse(&ev.mouse);
					goto tick;
			}
		} else if (e == timer){
			loaddir();
tick:
			redraw(screen);
		}
	}
end:
	free(proc);
	exits(nil);
}
