#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <thread.h>
#include <keyboard.h>
#include <mouse.h>

typedef struct Process Process;
struct Process {
	char user[1024];
	char cmd[1024];
	char args[2048];
	char state[1024];
	char r[1024];
	long u, s;
	int pid, siz;
};
Process *proclist;

enum {
	Emouse,
	Ekeyboard,
	Eresize,
	Etimer,
};

enum {
	Scrollwidth = 13,	/* scrollbar width */
	Vstep = 30,		/* vertical distance between cells */
	Hstep = 70,		/* minimum horizontal distance between cells */
	Theight = 25,		/* toolbar height */
	Thoffset = 15,		/* horziontal text offset */
	Tvoffset = 4,		/* vertical text offset */
};

char *headers[] = { 
	"pid", "user", "utime", "stime", "rtime",
	"size", "state", "command", "command + args", nil
};

int nprocs;
int visprocs;
int delay;
int scroffset;
int argumentflag;
int realtimeflag;
int reverseoflag;
int isscrolling;
int oldbuttons;
int hstep;

Image *toolbg;
Image *toolfg;
Image *viewbg;
Image *viewfg;
Image *selbg;
Image *selfg;
Image *scrollbg;
Image *scrollfg;
Rectangle toolr;
Rectangle viewr;
Rectangle scrollr;
Rectangle scrposr;

Keyboardctl *kctl;
Mousectl *mctl;
Biobuf *b;

int getrowcount(void);
int cmp(void *va, void *vb);
void emouse(Mouse *m);
void ekeyboard(Rune k);
void eresize(void);
int scroll(int d);
void timerproc(void *c);
void redraw(void);
void drawprocfield(int *px, int py, char *fmt, ...);
void loaddir(void);
void loadtheme(void);
void usage(void);

int
getrowcount(void)
{
	int i, c;

	for(i=0, c=1; headers[i] != nil; i++){
		if((i==4 && !realtimeflag) || (i==7 && argumentflag) || (i==8 && !argumentflag))
			continue;
		c++;
	}
	return c;
}

int
cmp(void *va, void *vb)
{
	Process *a = (Process*)va;
	Process *b = (Process*)vb;
	if(reverseoflag)
		return b->pid - a->pid;
	return a->pid - b->pid;
}

void
emouse(Mouse *m)
{
	if(oldbuttons==0 && m->buttons!=0 && ptinrect(m->xy, scrollr))
		isscrolling = 1;
	else if(m->buttons==0)
		isscrolling = 0;

	if(ptinrect(m->xy, scrollr)){
		switch(m->buttons){
		case 1:
			scroll(scroffset-5);
			break;
		case 4:
			scroll(scroffset+5);
			break;
		}
	}
	switch(m->buttons){
	case 8:
		scroll(scroffset-1);
		break;
	case 16:
		scroll(scroffset+1);
		break;
	}
	if(m->buttons&2 && isscrolling){
		scroll((m->xy.y - scrollr.min.y)*nprocs/Dy(scrollr));
	}
	if(ptinrect(m->xy, viewr)){
		/* TODO: highlight
		 * hovering = (m->xy.y-viewr.min.y)/30;
		 * hy = screen->r.min.y+Pvoffset+5+(hovering*Vstep);
		 * Rect(screen->r.min.x, hy, screen->r.max.x, hy+30)
         */
	}
	oldbuttons = m->buttons;
}

void
ekeyboard(Rune k)
{
	switch(k){
	case 'q': /* FALLTHROUGH */
	case Kdel:
		threadexitsall(nil);
		break;
	case Kdown:
		scroll(scroffset+1);
		break;
	case Kup:
		scroll(scroffset-1);
		break;
	case Kpgdown:
		scroll(scroffset+10);
		break;
	case Kpgup:
		scroll(scroffset-10);
		break;
	}
}

void
eresize(void)
{
	if(getwindow(display, Refnone) < 0)
		sysfatal("getwindow: %r");
	redraw();
}

int
scroll(int d)
{
	scroffset = d;
	if(scroffset+visprocs > nprocs)
		scroffset = nprocs-visprocs;
	else if(scroffset<0)
		scroffset = 0;
	redraw();
	return scroffset;
}

void
timerproc(void *c)
{
	threadsetname("timer");
	for(;;){
		sleep(delay);
		sendul(c, 0);
	}
}

void
drawprocfield(int *px, int py, char *fmt, ...)
{
	char buf[1024];
	int n;

	if((n = snprint(buf, sizeof(buf), "")) < 0)
		sysfatal("snprint: %r");
	va_list ap;
	va_start(ap, fmt);
	vseprint(buf+n, buf+sizeof(buf), fmt, ap);
	va_end(ap);

	string(screen, Pt(screen->r.min.x+Dx(scrollr)+Thoffset+*px, screen->r.min.y+Dy(toolr)+Tvoffset+py),
		viewfg, ZP, display->defaultfont, buf);
	*px += hstep;
}

void
redraw(void)
{
	int i, hscr, yscr, toffset, procx, procy;

	toolr = Rect(screen->r.min.x, screen->r.min.y, screen->r.max.x, screen->r.min.y+Theight);
	viewr = Rect(screen->r.min.x, screen->r.min.y+Dy(toolr), screen->r.max.x, screen->r.max.y);
	scrollr = Rect(screen->r.min.x, screen->r.min.y+Dy(toolr)+1, screen->r.min.x+Scrollwidth, screen->r.max.y);
	visprocs = (Dy(screen->r)-Dy(toolr)+Tvoffset)/Vstep;

	draw(screen, toolr, toolbg, nil, ZP);
	draw(screen, viewr, viewbg, nil, ZP);
	line(screen, Pt(screen->r.min.x, screen->r.min.y+Theight), Pt(screen->r.max.x, screen->r.min.y+Theight),
		Endsquare, Endsquare, 0, toolfg, ZP);

	yscr = toffset = 0;
	if(nprocs>visprocs){
		hscr = ((double)visprocs/nprocs)*Dy(scrollr);
		yscr = ((double)scroffset/nprocs)*Dy(scrollr);
		scrposr = Rect(scrollr.min.x, scrollr.min.y+yscr, scrollr.max.x-1, scrollr.min.y+yscr+hscr);
	}else{
		scrposr = Rect(scrollr.min.x, scrollr.min.y+yscr, scrollr.max.x-1, scrollr.max.y);
	}
	draw(screen, scrollr, scrollbg, nil, ZP);
	draw(screen, scrposr, scrollfg, nil, ZP);

	if ((hstep = Dx(screen->r)/getrowcount()) < Hstep)
		hstep = Hstep;

	for(i=0; headers[i]!=nil; i++){
		if((i==4 && !realtimeflag) || (i==7 && argumentflag) || (i==8 && !argumentflag))
			continue;
		string(screen, Pt(screen->r.min.x+Dx(scrollr)+Thoffset+toffset,
			screen->r.min.y+(Dy(toolr)/2 - display->defaultfont->height/2) ),
			toolfg, ZP, display->defaultfont, headers[i]);
		toffset += hstep;
	}

	for(i=scroffset; i<nprocs; i++){
		procx = 0;
		procy = Vstep*(i-scroffset);
		if(screen->r.min.y+Dy(toolr)+procy+display->defaultfont->height > screen->r.max.y)
			break;
		drawprocfield(&procx, procy, "%d", proclist[i].pid);
		drawprocfield(&procx, procy, "%s", proclist[i].user);
		drawprocfield(&procx, procy, "%lud:%.2lud", proclist[i].u/60, proclist[i].u%60);
		drawprocfield(&procx, procy, "%lud:%.2lud", proclist[i].s/60, proclist[i].s%60);
		if(realtimeflag)
			drawprocfield(&procx, procy, "%s", proclist[i].r);
		drawprocfield(&procx, procy, "%dK", proclist[i].siz);
		drawprocfield(&procx, procy, "%s", proclist[i].state);
		if(argumentflag){
			if(strlen(proclist[i].args) < 2)
				snprint(proclist[i].args, sizeof(proclist[i].args), "%s ?", proclist[i].cmd);
			drawprocfield(&procx, procy, "%s", proclist[i].args);
		}else{
			drawprocfield(&procx, procy, "%s", proclist[i].cmd);
		}
	}


	flushimage(display, 1);
}

void
loaddir(void)
{
	char status[4096];
	char buf[50];
	char* av[16];
	Dir* dir;
	int i, n, ac, fd, statfd, ndirs, pid;
	long rtime;

	if(proclist != nil)
		free(proclist);

	if(chdir("/proc") == -1)
		sysfatal("chdir: %r");
	if((fd = open(".", OREAD)) < 0)
		sysfatal("open: %r");
	if((ndirs = dirreadall(fd, &dir)) < 0)
		sysfatal("dirreadall: %r");
	if((proclist = malloc(ndirs*sizeof(Process))) == nil)
		sysfatal("malloc: %r");

	nprocs = 0;
	for(i=0; i<ndirs; i++) {
		if(strcmp(dir[i].name,"trace") == 0)
			continue;
		pid = atoi(dir[i].name);
		sprint(buf, "/proc/%d/status", pid);
		if((statfd = open(buf, OREAD)) < 0)
			continue;
		if((n = read(statfd, status, sizeof(status) - 1)) < 0){
			close(statfd);
			continue;
		}
		status[n] = '\0';
		if((ac = tokenize(status, av, nelem(av)-1)) < 12)
			continue;
		av[ac] = 0;
		close(statfd);
		close(n);

		proclist[nprocs].pid = pid;
		strcpy(proclist[nprocs].cmd, av[0]);
		strcpy(proclist[nprocs].user, av[1]);
		strcpy(proclist[nprocs].state, av[2]);
		proclist[nprocs].u = strtoul(av[3], 0, 0)/1000;
		proclist[nprocs].s = strtoul(av[4], 0, 0)/1000;
		proclist[nprocs].siz = atoi(av[9]);
		rtime = strtoul(av[5], 0, 0)/1000;

		if(realtimeflag){
			if(rtime >= 86400)
				sprint(proclist[i].r, "%lud:%02lud:%02lud:%02lud",
					rtime/86400, (rtime/3600)%24, (rtime/60)%60, rtime%60);
			else if(rtime >= 3600)
				sprint(proclist[i].r, "%lud:%02lud:%02lud",
					rtime/3600, (rtime/60)%60, rtime%60);
			else
				sprint(proclist[i].r, "%lud:%02lud", rtime/60, rtime%60);
		}

		if(argumentflag){
			sprint(buf, "/proc/%d/args", pid);
			if((statfd = open(buf, OREAD)) < 0){
				strcpy(proclist[nprocs].args, "?");
				goto skip;
			}
			if((n = read(statfd, buf, sizeof(buf) - 1)) < 0)
				goto skip;
			buf[n] = '\0';
			strcpy(proclist[nprocs].args, buf);
			close(statfd);
		}
skip:
		nprocs++;
	}
	close(fd);
	free(dir);
	qsort(proclist, nprocs, sizeof(Process), cmp);
}

void
loadtheme(void)
{
	char *s, *v[3];

	if((b = Bopen("/dev/theme", OREAD)) != nil){
		while((s = Brdline(b, '\n')) != nil){
			s[Blinelen(b)-1] = 0;
			if(!tokenize(s, v, nelem(v)))
				continue;
			if(strcmp(v[0], "ltitle") == 0)
				toolbg = allocimage(display, Rect(0,0,1,1),
					screen->chan, 1, strtoul(v[1], nil, 16)<<8 | 0xff);
			if(strcmp(v[0], "menutext") == 0)
				toolfg = allocimage(display, Rect(0,0,1,1),
					screen->chan, 1, strtoul(v[1], nil, 16)<<8 | 0xff);
			if(strcmp(v[0], "back") == 0)
				viewbg = allocimage(display, Rect(0,0,1,1),
					screen->chan, 1, strtoul(v[1], nil, 16)<<8 | 0xff);
			if(strcmp(v[0], "text") == 0)
				viewfg = allocimage(display, Rect(0,0,1,1),
					screen->chan, 1, strtoul(v[1], nil, 16)<<8 | 0xff);
			if(strcmp(v[0], "border") == 0)
				scrollbg = allocimage(display, Rect(0,0,1,1),
					screen->chan, 1, strtoul(v[1], nil, 16)<<8 | 0xff);
			if(strcmp(v[0], "back") == 0)
				scrollfg = allocimage(display, Rect(0,0,1,1),
					screen->chan, 1, strtoul(v[1], nil, 16)<<8 | 0xff);
		}
		Bterm(b);
	} else {
		toolbg = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DPalebluegreen);
		toolfg = display->black;
		viewbg = display->white;
		viewfg = display->black;
		selbg = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0xEFEFFFEE);
		selfg = display->black;
		scrollbg = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0x999999FF);
		scrollfg = display->white;
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-a] [-i] [-h] [-r] [-d] <seconds>\n", argv0);
	threadexitsall("usage");
}

void
threadmain(int argc, char *argv[])
{
	Mouse m;
	Rune k;
	Alt alts[] = {
		{ nil, &m,  CHANRCV },
		{ nil, &k,  CHANRCV },
		{ nil, nil, CHANRCV },
		{ nil, nil, CHANRCV },
		{ nil, nil, CHANEND },
	};

	delay = 5000;
	argumentflag = 0;
	reverseoflag = 0;
	realtimeflag = 0;
	hstep = Hstep;

	ARGBEGIN{
	case 'a':
		argumentflag++;
		break;
	case 'd':
		delay = atoi(EARGF(usage()))*1000;
		break;
	case 'h':
		usage();
	case 'i':
		reverseoflag++;
		break;
	case 'r':
		realtimeflag++;
		break;
	}
	ARGEND;

	if(initdraw(nil, nil, argv0) < 0)
		sysfatal("initdraw: %r");
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	if((kctl = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");

	alts[Emouse].c = mctl->c;
	alts[Ekeyboard].c = kctl->c;
	alts[Eresize].c = mctl->resizec;
	alts[Etimer].c = chancreate(sizeof(ulong), 0);
	proccreate(timerproc, alts[Etimer].c, 1024);
	loadtheme();
	loaddir();
	goto run;

	for(;;){
		switch(alt(alts)){
		case Emouse:
			emouse(&m);
			break;
		case Ekeyboard:
			ekeyboard(k);
			break;
		case Eresize:
			eresize();
			break;
		case Etimer:
			loaddir();
			break;
		}
run:
		redraw();
	}
}
