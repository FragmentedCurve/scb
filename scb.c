#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <ncurses.h>

#ifdef KEY_ENTER
#undef KEY_ENTER
#endif
#define KEY_ENTER 0x0A
#define KEY_ESC 27

#define DEFAULT_EDITOR "vim"
#define SCROLL_UP BUTTON4_PRESSED
#define SCROLL_DOWN 0x200000

struct Lines {
	char** lines;
	size_t len;
	size_t cap;
	size_t selected;
};

static char* search_term = NULL;

struct Lines* new_lines()
{
	const size_t cap = 1024;
	struct Lines* l = calloc(1, sizeof (struct Lines));
	assert(l);

	l->lines = malloc(cap * sizeof (char*));
	l->cap = cap;
	l->selected = -1;

	return l;
}

void add_line(struct Lines* l, char* line)
{
	size_t len = strlen(line);

	if (l->len == l->cap) {
		l->cap *= 2;
		l->lines = realloc(l->lines, l->cap * sizeof (char*));
		assert(l->lines);
	}

	if (line[len - 1] == '\n') {
		line[len - 1] = 0;
	}
	l->lines[l->len] = line;
	l->len++;
}

void select_down(struct Lines* l, size_t n)
{
	if ((l->selected + n) < l->len)
		l->selected += n;
	else
		l->selected = l->len - 1;
}

void select_up(struct Lines* l, size_t n)
{
	if (l->selected >= n)
		l->selected -= n;
	else
		l->selected = 0;
}

void select_line(struct Lines* l, size_t n)
{
	if (n < 0) {
		l->selected = 0;
	} else if (n >= l->len) {
		l->selected = l->len - 1;
	} else {
		l->selected = n;
	}
}

bool find(struct Lines* l, char* substr)
{
	if (!substr)
		return false;

	for (size_t i = l->selected + 1; i < l->len; i++) {
		char* tok = strstr(l->lines[i], substr);
		if (tok) {
			l->selected = i;
			return true;
		}
	}

	return false;
}

void reset_stds()
{
	FILE* fd = fopen("/dev/tty", "r+");
	close(0);
	dup(fileno(fd));
	close(1);
	dup(fileno(fd));
	fclose(fd);
}

void init_screen()
{
	reset_stds();
	initscr();
	cbreak();
	noecho();
	curs_set(0);
	clear();
}

void auto_select(struct Lines* l)
{
	int rows, col;
	(void) col; /* GCC claims col is unused. clang behaves. */

	/* TODO: Separate this code from ncurses. */
	getmaxyx(stdscr, rows, col);

	if (l->len < rows) {
		l->selected = l->len / 2;
	} else {
		l->selected = rows / 2;
	}
}

#define TAB_STRING "    "
#define TAB_LENGTH (sizeof (TAB_STRING))

void draw_line(int row, const char* line, bool highlight)
{
	int rows, cols;
	size_t len = strlen(line);
	int char_count = 0;

	(void) rows; /* Compiler is complaining that rows is unused */

	if (highlight)
		attrset(A_STANDOUT);
	else
		attrset(A_NORMAL);

	getmaxyx(stdscr, rows, cols);

	/*
	  Using mvprintw causes the x,y coords to get
	  out of sync when tabs are in the line.

	  Therefore, replace the tabs with spaces and
	  keep track of the char bytes.

	  All in the name of highlighting the entire row.
	*/

	/* mvprintw(row, 0, "%.*s", cols, line); */
	for (int i = 0; i < len; i++) {
		if (line[i] == '\t') {
			mvprintw(row, char_count, TAB_STRING);
			char_count += TAB_LENGTH - 1;
		} else {
			mvaddch(row, char_count, line[i]);
			char_count++;
		}
	}

	/* Fill the rest of the row with spaces to highlight the entire row. */
	if (cols > char_count)
		mvprintw(row, char_count, "%*c", cols - char_count, ' ');
}

void draw_frame(struct Lines* l, size_t offset)
{
	int rows, cols;
	(void) cols; /* GCC claims col is unused. clang behaves. */
	getmaxyx(stdscr, rows, cols);
	rows--;

	for (int i = offset; (i - offset) < rows && i < l->len; i++) {
		bool highlight = i == l->selected;
		draw_line(i - offset, l->lines[i], highlight);
	}
}

const char* status_msg(const char* msg)
{
	static const char* current = NULL;
	const char* old = current;
	current = msg;
	return old;
}

void draw_statusbar(struct Lines* l)
{
	int n;
	int rows, cols;
	(void) cols; /* GCC claims col is unused. clang behaves. */
	const char* msg = status_msg(NULL);

	getmaxyx(stdscr, rows, cols);
	attrset(A_DIM);
	mvprintw(rows - 1, 0, ":: scb (%lu,%zu) :::: %n", l->selected + 1, l->len, &n);

	if (msg) {
		attrset(A_BLINK | A_DIM);
		mvprintw(rows - 1, n, "%s", msg);
	}
}

void draw(struct Lines* l)
{
	size_t offset;
	int rows, cols;
	(void) cols; /* GCC claims col is unused. clang behaves. */

	clear();
	getmaxyx(stdscr, rows, cols);
	rows--; /* Leave the bottom row for the status bar */

	if (l->selected <= rows / 2) {
		offset = 0;
	} else if (l->selected >= l->len - (rows / 2)){
		offset = l->len - rows;
	} else {
		offset = l->selected - (rows / 2);
	}

	draw_frame(l, offset);
	draw_statusbar(l);
	refresh();
}

void parse_line(char* line, char** file, char** linenum)
{
	char* tok = strchr(line, ':');

	*file = NULL;
	*linenum = NULL;

	if (!tok) {
		/* TODO: Why are we strduping this? */
		*file = strdup(line);
		return;
	}

	*file = strndup(line, tok - line);
	tok = strchr(tok + 1, ':');
	if (!tok) {
		/* TODO: Why are we setting this to NULL, again? */
		*linenum = NULL;
		return;
	}
	*linenum = line + strlen(*file) + 1;
	*linenum = strndup(*linenum - 1, tok - *linenum + 1);
	**linenum = '+';
}

void open_line(char* line)
{
	int status;
	char* file;
	char* linenum;
	char* editor = getenv("EDITOR");

	if (!editor) {
		editor = DEFAULT_EDITOR;
	}

	/* TODO: Check if editor exists. This saves us from worrying about
	execlp failing due to the command not being found. */

	parse_line(line, &file, &linenum);

	if (!file || !linenum) {
		/* TODO: Report this error on the status bar. */
		clear();
		mvprintw(0, 0, "Error parsing line.");
		refresh();
		getch();
		return;
	}

	/* TODO: Do error checking and reporting on the parsed line. */

	def_prog_mode();
	endwin();
	pid_t pid = fork();
	if (pid > 0) {
		free(file);
		free(linenum);
		waitpid(pid, &status, 0);
		if (WEXITSTATUS(status) != 0) {
			(void) status_msg("Failed to open editor.");
		}
		reset_prog_mode();
		refresh();
	} else if (pid == 0) {
		if (execlp(editor, editor, linenum, file, NULL) == -1) {
			fprintf(stderr, "Failed to run %s: %s\n", editor, strerror(errno));
			exit(2);
		}
	} else {
		fprintf(stderr, "Failed to fork: %s\n", strerror(errno));
		exit(1);
	}
}

char* command_input(const char* prefix)
{
	int c;
	int last, rows, cols;
	(void) cols; /* GCC claims col is unused. clang behaves. */
	char* buf = malloc(512);
	char* p = buf;

	assert(buf);

	getmaxyx(stdscr, rows, cols);
	attrset(A_DIM);
	last = rows - 1;
	echo();
	curs_set(1);
	mvprintw(last, 0, "%s ", prefix);
	clrtoeol();
	nocbreak();
	while ((c = getch()) != KEY_ENTER) {
		if (buf - p == 512 - 1) {
			break;
		}
		*p = c;
		p++;
	}
	*p = '\0';

	noecho();
	curs_set(0);
	cbreak();
	return buf;
}

void input_loop(struct Lines* l)
{
	int c;

	mousemask(ALL_MOUSE_EVENTS, NULL);
	keypad(stdscr, TRUE);
	auto_select(l);
	draw(l);

	while ((c = getch()) != 'q') {
		switch (c) {
			case KEY_MOUSE: {
				MEVENT event;
				if (getmouse(&event) == OK) {
					if (event.bstate & BUTTON3_CLICKED) {
						open_line(l->lines[l->selected]);
					} else if (event.bstate & SCROLL_UP) {
						select_up(l, 1);
					} else if (event.bstate & SCROLL_DOWN) {
						select_down(l, 1);
					}
				}
			} break;
			case KEY_UP: {
				select_up(l, 1);
			} break;
			case KEY_DOWN: {
				select_down(l, 1);
			} break;
			case KEY_PPAGE: {
				int rows, col;
				(void) col; /* GCC claims col is unused. clang behaves. */
				getmaxyx(stdscr, rows, col);
				select_up(l, rows / 2);
			} break;
			case KEY_NPAGE: {
				int rows, col;
				(void) col; /* GCC claims col is unused. clang behaves. */
				getmaxyx(stdscr, rows, col);
				select_down(l, rows / 2);
			} break;
			case KEY_ENTER: {
				open_line(l->lines[l->selected]);
			} break;
			case KEY_HOME: {
				select_line(l, 0);
			} break;
			case KEY_END: {
				select_line(l, l->len);
			} break;
			case '/': {
				free(search_term);
				search_term = command_input(" search: ");
				if (search_term) {
					find(l, search_term);
				}
			} break;
			case 'n': {
				if (search_term) {
					find(l, search_term);
				} else {
					search_term = command_input(" search: ");
					find(l, search_term);
				}
			} break;
			case ':': {
				char* t = command_input(" goto: ");
				select_line(l, atoi(t) - 1);
				free(t);
			} break;
			default: {
				status_msg("See the manpage for help");
			} break;
		}
		draw(l);
	}
}

int main(int argc, char** argv)
{
	struct Lines* l = new_lines();

	{
		char* line = NULL;
		size_t len = 0;

		while (getline(&line, &len, stdin) != -1) {
			add_line(l, line);
			line = NULL;
			len = 0;
		}
	}

	if (l->len == 0) {
		fprintf(stderr, "Nothing to do.\n");
		return 0;
	}

	init_screen();
	input_loop(l);
	endwin();

	/* Clean up memory */
	for (size_t i = 0; i < l->len; i++) {
		free(l->lines[i]);
	}
	free(l->lines);
	free(l);
	free(search_term);

	return 0;
}
