#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <thread.h>
#include <keyboard.h>
#include <mouse.h>

void redraw(void);

typedef struct Process Process;
struct Process {
	char user[28], cmd[28], state[28], note[13];
	char args[64], r[11];
	ulong u, s, siz;
	int pid;
} *proclist;

enum {
	Delay = 5000, 		/* default delay in ms */
	Scrollwidth = 13,	/* scrollbar width */
	Minwidth = 80,		/* Minimum amount of characters to render a line without it looking too terrible :) */
	Theight = 25,		/* toolbar height */
	Rheight = 25,		/* row height */
};

enum {
	Spid = 1<<0,		/* sort by PID (default behaviour in ps(1)) */
	Susr = 1<<1,		/* sort by the user name */
	Sutime = 1<<2,		/* sort by user time */
	Sstime = 1<<3,		/* sort by system time */
	Srtime = 1<<4,		/* sort by real time */
	Smem = 1<<5,		/* sort by size */
	Sstate = 1<<6,		/* sort by the state */
	Scmd= 1<<7			/* sort by the command (excluding arguments) */
};

Keyboardctl *kctl;
Mousectl *mctl;
char *mmenustr[] = { "Send note", "stop", "start", "kill", 0 };
char *rmenustr[] = {
	"Sort by",
	"pid",
	"user",
	"utime",
	"stime",
	"rtime",
	"size",
	"state",
	"cmd",
	"reversed order",
	" ",
	"Display options",
	"cmd arguments",
	"real time",
	"note group",
	" ",
	"exit",
	0
};

Menu mmenu = { mmenustr }, rmenu = { rmenustr };

// NOTE(thimc): the header names are in order and need to be so, not only
// for the visual part but also for the logic that determines if the field itself
// (and the data from each process) should be rendered or not.
struct hdr {
    char *name;
    int width;
} hdrs[] = {
	{"pid", 7},
	{"noteid", 7},
	{"user", 7},
	{"utime", 6},
	{"stime", 6},
	{"rtime", 8},
	{"size", 8},
	{"state", 13},
	{"cmd", 0},
	{"cmd + args", 0},
};

int argumentflag, noteflag, realtimeflag, reverseflag, sorttypeflag;
int sorttype, nprocs, visprocs, highlighted;
int delay = Delay;
int hstep;
int scroffset, isscrolling, oldbuttons;

Image *toolbg, *toolfg;
Image *viewbg, *viewfg;
Image *scrollbg, *scrollfg;
Image *selbg;
Rectangle toolr;
Rectangle viewr;
Rectangle scrollr, scrposr;

int
skipcolumn(int i)
{
	return (i == 1 && !noteflag)
		|| (i == 5 && !realtimeflag)
		|| (i == 8 && argumentflag)
		|| (i == 9 && !argumentflag);
}

int
sort(void *va, void *vb)
{
	#define cmp(a, b) (reverseflag ? (b - a) : (a - b))
	Process *a = (Process*)va, *b = (Process*)vb;

	if(sorttype&Spid) return cmp(a->pid, b->pid);
	if(sorttype&Susr)
		return cmp(strcmp(a->user, b->user), -strcmp(a->user, b->user));
	if(sorttype&Sutime)
		return cmp(a->u, b->u);
	if(sorttype&Sstime)
		return cmp(a->s, b->s);
	if(sorttype&Srtime)
		return cmp(strcmp(a->r, b->r), -strcmp(a->r, b->r));
	if(sorttype&Smem)
		return cmp(a->siz, b->siz);
	if(sorttype&Sstate)
		return cmp(strcmp(a->state, b->state), -strcmp(a->state, b->state));
	if(sorttype&Scmd){
		if(!argumentflag)
			return cmp(strcmp(a->cmd, b->cmd), -strcmp(a->cmd, b->cmd));
		else
			return cmp(strcmp(a->cmd, b->args), -strcmp(a->cmd, b->args));
	}
	return 0;
}

char*
formatbytes(ulong mem)
{
	static char out[12];
	const char *unit = "BKMGTPZYREQ?";
	int n;
	double siz;

	n = 0;
	for(siz = (double)mem * 1024.0; siz >= 1024.0;){
		siz /= 1024.0;
		n++;
	}
	if(n >= strlen(unit) || snprint(out, sizeof(out), "%lud%c", (ulong)siz, unit[n]) < 1)
		strncat(out, "?", sizeof(out));
	return out;
}

int
scroll(int d)
{
	scroffset = d;
	if(scroffset + visprocs > nprocs)
		scroffset = nprocs-visprocs;
	if(scroffset < 0)
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

Point
getpos(int x, int y)
{
	Point pt;
	int i;

	pt = Pt(Dx(scrollr) * 2, (Rheight - font->height) / 2);
	for(i = 0; i < x; i++){
		if(skipcolumn(i)) continue;
		pt.x += hdrs[i].width * font->width;
	}
	pt.y += (Rheight * y + 1);

	return addpt(pt, screen->r.min);
}

void
drawcell(int x, int y, char *fmt, ...)
{
	char buf[128];
	va_list ap;

	va_start(ap, fmt);
	vseprint(buf, buf+sizeof(buf), fmt, ap);
	va_end(ap);

	string(screen, getpos(x, y), viewfg, ZP, display->defaultfont, buf);
}

Rectangle
getrow(int y)
{
	Point pt;

	pt = getpos(0, y);
	pt.x = screen->r.min.x + Dx(scrollr) + 2;
	pt.y += Dy(toolr) - (Rheight - font->height) / 2;
	return Rect(pt.x, pt.y, pt.x + screen->r.max.x, pt.y + Rheight);
}

void
redraw(void)
{
	Point pt, spt;
	int i, x, y;

	toolr = viewr = screen->r;
	toolr.max.y = screen->r.min.y + Theight;
	viewr.min.y += Dy(toolr);
	scrollr = Rect(screen->r.min.x + 1, screen->r.min.y + Dy(toolr) + 2, screen->r.min.x + Scrollwidth, screen->r.max.y - 1);

	draw(screen, toolr, toolbg, nil, ZP);
	draw(screen, viewr, viewbg, nil, ZP);
	line(screen, addpt(screen->r.min, Pt(0, Theight)), Pt(screen->r.max.x, screen->r.min.y + Theight), Endsquare, Endsquare, 0, toolfg, ZP);

	spt = Pt(((double)visprocs / nprocs) * Dy(scrollr), ((double)scroffset / nprocs) * Dy(scrollr));
	scrposr = scrollr;
	scrposr.min.y += spt.y;
	scrposr.max.x -= 1;
	if(nprocs > visprocs)
		scrposr.max.y = scrollr.min.y + spt.y + spt.x;
	else
		scrposr.max.y = scrollr.min.y + spt.y;
	draw(screen, scrollr, scrollbg, nil, ZP);
	draw(screen, scrposr, scrollfg, nil, ZP);

	for(i = 0; i < nelem(hdrs); i++){
		if(skipcolumn(i)) continue;
		drawcell(i, 0, "%s", hdrs[i].name);
	}
	visprocs = (Dy(viewr) / Rheight);
	for(i = scroffset; i < nprocs; i++){
		x = 0;
		y = i - scroffset;
		if(y > visprocs) break;
		if(highlighted == y)
			draw(screen, getrow(y), selbg, nil, ZP);
		y++;
		drawcell(x++, y, "%d", proclist[i].pid);
		if(noteflag)
			drawcell(x++, y, "%s", proclist[i].note);
		else
			x++;
		drawcell(x++, y,  "%-*s", sizeof(hdrs[y].width), proclist[i].user);
		drawcell(x++, y, "%1lud:%.2lud", proclist[i].u/60, proclist[i].u%60);
		drawcell(x++, y, "%1lud:%.2lud", proclist[i].s/60, proclist[i].s%60);
		if(realtimeflag)
			drawcell(x++, y, "%-*s", sizeof(hdrs[y].width), proclist[i].r);
		else
			x++;
		drawcell(x++, y, "%-*s", sizeof(hdrs[pt.x].width), formatbytes(proclist[i].siz));
		drawcell(x++, y, "%-*s", sizeof(hdrs[pt.x].width), proclist[i].state);
		if(argumentflag)
			drawcell(x++, y, "%-s", proclist[i].args);
		else
			drawcell(x++, y, "%-s", proclist[i].cmd);
		USED(x, y);
	}
	flushimage(display, 1);
}

void
loaddir(void)
{
	Dir *d;
	char status[4096], buf[50], *sf[13];
	ulong rtime;
	int fd, ndirs, n, i, pid, statfd, sn, sc;

	n = 0;
	if(chdir("/proc") < 0)
		sysfatal("chdir: %r");
	if((fd = open(".", OREAD)) < 0)
		sysfatal("open: %r");
	if((ndirs = dirreadall(fd, &d)) < 0)
		sysfatal("dirreadall: %r");
	if(proclist != nil)
		free(proclist);
	if((proclist = malloc(ndirs * sizeof(*proclist))) == nil)
		sysfatal("malloc: %r");

	for(i = 0; i < ndirs; i++){
		if(strcmp(d[i].name,"trace") == 0 || (pid = atoi(d[i].name)) < 0)
			continue;
		snprint(buf, sizeof(buf), "/proc/%d/status", pid);
		if((statfd = open(buf, OREAD)) < 0) continue;
		if((sn = read(statfd, status, sizeof(status))) < 0)
			goto cleanup;
		status[sn] = 0;
		if((sc = tokenize(status, sf, nelem(sf) - 1)) < nelem(sf) - 1)
			goto cleanup;
		sf[sc] = 0;
		close(statfd);

		proclist[n].pid = pid;
		strcpy(proclist[n].cmd, sf[0]);
		strcpy(proclist[n].user, sf[1]);
		strcpy(proclist[n].state, sf[2]);
		proclist[n].u = strtoul(sf[3], 0, 0) / 1000;
		proclist[n].s = strtoul(sf[4], 0, 0) / 1000;
		proclist[n].siz  = strtoul(sf[9], 0, 0);

		if(realtimeflag){
			rtime = strtoul(sf[5], 0, 0) / 1000;
			if(rtime >= 86400)
				snprint(proclist[n].r, sizeof(proclist[n].r), "%lud:%02lud:%02lud:%02lud", rtime / 86400, (rtime / 3600) % 24, (rtime / 60) % 60, rtime % 60);
			else if(rtime >= 3600)
				snprint(proclist[n].r, sizeof(proclist[n].r), "%lud:%02lud:%02lud", rtime / 3600, (rtime / 60) % 60, rtime % 60);
			else
				snprint(proclist[n].r, sizeof(proclist[n].r), "%lud:%02lud", rtime / 60, rtime % 60);
		}
		if(argumentflag){
			close(statfd);
			sprint(buf, "/proc/%d/args", pid);
 			if((statfd = open(buf, OREAD)) < 0)
				goto errargs;
			if((sn = read(statfd, buf, sizeof(buf))) < 1)
				goto errargs;
			buf[sn] = 0;
			strncpy(proclist[n].args, buf, sizeof(proclist[n].args));
			proclist[n].args[sizeof(proclist[n].args) - 1] = 0;
			goto doneargs;
errargs:
			snprint(proclist[n].args, sizeof(proclist[n].args), "%s ?", proclist[n].cmd);
		}
doneargs:
		if(noteflag){
			close(statfd);
			sprint(buf, "/proc/%d/noteid", pid);
			if((statfd = open(buf, OREAD)) < 0)
				goto cleanup;
			if((sn = read(statfd, buf, sizeof(buf))) < 1)
				goto cleanup;
			snprint(proclist[n].note, sizeof(proclist[n].note), "%ud", atoi(buf));
			USED(sn);
			close(statfd);
		}
		n++;
cleanup:
		close(statfd);
	}
	close(fd);
	free(d);
	nprocs = n;

	qsort(proclist, nprocs, sizeof(*proclist), sort);
}

void
loadtheme(void)
{
	Biobuf *b;
	char *s, *v[3];

	selbg = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0xF0F0F0FF);

	if((b = Bopen("/dev/theme", OREAD)) != nil){
		while((s = Brdline(b, '\n')) != nil){
			s[Blinelen(b) - 1] = 0;
			if(!tokenize(s, v, nelem(v)))
				continue;
			if(strcmp(v[0], "ltitle") == 0)
				toolbg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, strtoul(v[1], nil, 16)<<8 | 0xff);
			if(strcmp(v[0], "menutext") == 0)
				toolfg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, strtoul(v[1], nil, 16)<<8 | 0xff);
			if(strcmp(v[0], "back") == 0)
				viewbg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, strtoul(v[1], nil, 16)<<8 | 0xff);
			if(strcmp(v[0], "text") == 0)
				viewfg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, strtoul(v[1], nil, 16)<<8 | 0xff);
			if(strcmp(v[0], "border") == 0)
				scrollbg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, strtoul(v[1], nil, 16)<<8 | 0xff);
			if(strcmp(v[0], "back") == 0)
				scrollfg = allocimage(display, Rect(0, 0, 1, 1), screen->chan, 1, strtoul(v[1], nil, 16)<<8 | 0xff);
		}
		Bterm(b);
		return;
	}
	toolbg = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DPalebluegreen);
	toolfg = display->black;
	viewbg = display->white;
	viewfg = display->black;
	scrollbg = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0x999999FF);
	scrollfg = display->white;
}

void
mmenuhit(void)
{
	int i, n;

	if((i = menuhit(2, mctl, &mmenu, nil)) < 1)
		return;
	n = highlighted + scroffset;
	if(n < 1 || n > nprocs)
		return;

	postnote(PNGROUP, proclist[n].pid, mmenustr[i]);
	loaddir();
}

void
rmenuhit(void)
{
	int ok;

	ok = 0;
	switch(menuhit(3, mctl, &rmenu, nil)){
	case 1: sorttype = Spid; ok = 1; break;
	case 2: sorttype = Susr; ok = 1; break;
	case 3: sorttype = Sutime; ok = 1; break;
	case 4: sorttype = Sstime; ok = 1; break;
	case 5: sorttype = Srtime; ok = 1; break;
	case 6: sorttype = Smem; ok = 1; break;
	case 7: sorttype = Sstate; ok = 1; break;
	case 8: sorttype = Scmd; ok = 1; break;
	case 9: reverseflag = !reverseflag; ok = 1; break;
	case 12: argumentflag = !argumentflag; ok = 2; break;
	case 13: realtimeflag = !realtimeflag; ok = 2; break;
	case 14: noteflag = !noteflag; ok = 2; break;
	case 16: threadexitsall(nil);
	}
	if(ok){
		if(ok > 1){
			loaddir();
			scroll(scroffset);
		}
		qsort(proclist, nprocs, sizeof(*proclist), sort);
		redraw();
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-ahinr] [-d seconds] [-s fmt]\n", argv0);
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
	char *sfmt;
	int i;

	sorttype |= Spid;
	ARGBEGIN{
	case 'a': argumentflag++; break;
	case 'd': delay = atoi(EARGF(usage()))*1000; break;
	case 'h': usage();
	case 'i': reverseflag++; break;
	case 'n': noteflag++; break;
	case 'r': realtimeflag++; break;
	case 's':
		sfmt = EARGF(usage());
		do{
			switch(*sfmt){
			case 'p': sorttype |= Spid; break;
			case 'U': sorttype |= Susr; break;
			case 'u': sorttype |= Sutime; break;
			case 's': sorttype |= Sstime; break;
			case 'r': sorttype |= Srtime; break;
			case 'm': sorttype |= Smem; break;
			case 'S': sorttype |= Sstate; break;
			case 'c': sorttype |= Scmd; break;
			default:
				if(*sfmt == 0) break;
				usage();
			}
		}while(*sfmt++);
		break;
	}ARGEND;

	if(initdraw(nil, nil, argv0) < 0)
		sysfatal("initdraw: %r");
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	if((kctl = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");

	alts[0].c = mctl->c;
	alts[1].c = kctl->c;
	alts[2].c = mctl->resizec;
	alts[3].c = chancreate(sizeof(ulong), 0);
	proccreate(timerproc, alts[3].c, 1024);
	loadtheme();
	loaddir();
	goto run;

	for(;;){
		switch(alt(alts)){
		case 0:
			if(oldbuttons == 0 && m.buttons != 0 && ptinrect(m.xy, scrollr))
				isscrolling = 1;
			else if(m.buttons == 0)
				isscrolling = 0;
			else if(m.buttons == 4)
				rmenuhit();
			if(ptinrect(m.xy, scrollr)){
				switch(m.buttons){
				case 1: scroll(scroffset - 5); break;
				case 4: scroll(scroffset + 5); break;
				}
			}
			highlighted = -1;
			if(ptinrect(m.xy, viewr)){
				for(i = 0; i < visprocs; i++){
					if(ptinrect(m.xy, getrow(i)) && !ptinrect(m.xy, scrollr)){
						highlighted = i;
						if(m.buttons & 2)
							mmenuhit();
						break;
					}
				}
			}
			switch(m.buttons){
			case 8: scroll(scroffset - 1); break;
			case 16: scroll(scroffset + 1); break;
			}
			if(m.buttons & 2 && isscrolling){
				scroll((m.xy.y - scrollr.min.y) * nprocs / Dy(scrollr));
			}
			oldbuttons = m.buttons;
			break;
		case 1:
			switch(k){
			case 'q':
			case Kdel: threadexitsall(nil); break;
			case Kdown: scroll(scroffset+1); break;
			case Kup: scroll(scroffset-1); break;
			case Kpgdown: scroll(scroffset+10); break;
			case Kpgup: scroll(scroffset-10); break;
			case Khome: scroll(0); break;
			case Kend: scroll(nprocs); break;
			}
			break;
		case 2:
			if(getwindow(display, Refnone) < 0)
				sysfatal("getwindow: %r");
			break;
		case 3: loaddir(); break;
		}
run:
		redraw();
	}
}
