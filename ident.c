/*
 * ident.c
 *
 * create git identifier lines of the form "name <email> date"
 *
 * Copyright (C) 2005 Linus Torvalds
 */
#include "cache.h"

static char git_default_date[50];

static void copy_gecos(const struct passwd *w, char *name, size_t sz)
{
	char *src, *dst;
	size_t len, nlen;

	nlen = strlen(w->pw_name);

	/* Traditionally GECOS field had office phone numbers etc, separated
	 * with commas.  Also & stands for capitalized form of the login name.
	 */

	for (len = 0, dst = name, src = w->pw_gecos; len < sz; src++) {
		int ch = *src;
		if (ch != '&') {
			*dst++ = ch;
			if (ch == 0 || ch == ',')
				break;
			len++;
			continue;
		}
		if (len + nlen < sz) {
			/* Sorry, Mr. McDonald... */
			*dst++ = toupper(*w->pw_name);
			memcpy(dst, w->pw_name + 1, nlen - 1);
			dst += nlen - 1;
		}
	}
	if (len < sz)
		name[len] = 0;
	else
		die("Your parents must have hated you!");

}

static void copy_email(const struct passwd *pw)
{
	/*
	 * Make up a fake email address
	 * (name + '@' + hostname [+ '.' + domainname])
	 */
	size_t len = strlen(pw->pw_name);
	if (len > sizeof(git_default_email)/2)
		die("Your sysadmin must hate you!");
	memcpy(git_default_email, pw->pw_name, len);
	git_default_email[len++] = '@';
	gethostname(git_default_email + len, sizeof(git_default_email) - len);
	if (!strchr(git_default_email+len, '.')) {
		struct hostent *he = gethostbyname(git_default_email + len);
		char *domainname;

		len = strlen(git_default_email);
		git_default_email[len++] = '.';
		if (he && (domainname = strchr(he->h_name, '.')))
			strlcpy(git_default_email + len, domainname + 1,
				sizeof(git_default_email) - len);
		else
			strlcpy(git_default_email + len, "(none)",
				sizeof(git_default_email) - len);
	}
}

static void setup_ident(void)
{
	struct passwd *pw = NULL;

	/* Get the name ("gecos") */
	if (!git_default_name[0]) {
		pw = getpwuid(getuid());
		if (!pw)
			die("You don't exist. Go away!");
		copy_gecos(pw, git_default_name, sizeof(git_default_name));
	}

	if (!git_default_email[0]) {
		const char *email = getenv("EMAIL");

		if (email && email[0])
			strlcpy(git_default_email, email,
				sizeof(git_default_email));
		else {
			if (!pw)
				pw = getpwuid(getuid());
			if (!pw)
				die("You don't exist. Go away!");
			copy_email(pw);
		}
	}

	/* And set the default date */
	if (!git_default_date[0])
		datestamp(git_default_date, sizeof(git_default_date));
}

static int add_raw(char *buf, size_t size, int offset, const char *str)
{
	size_t len = strlen(str);
	if (offset + len > size)
		return size;
	memcpy(buf + offset, str, len);
	return offset + len;
}

static int crud(unsigned char c)
{
	static char crud_array[256];
	static int crud_array_initialized = 0;

	if (!crud_array_initialized) {
		int k;

		for (k = 0; k <= 31; ++k) crud_array[k] = 1;
		crud_array[' '] = 1;
		crud_array['.'] = 1;
		crud_array[','] = 1;
		crud_array[':'] = 1;
		crud_array[';'] = 1;
		crud_array['<'] = 1;
		crud_array['>'] = 1;
		crud_array['"'] = 1;
		crud_array['\''] = 1;
		crud_array_initialized = 1;
	}
	return crud_array[c];
}

/*
 * Copy over a string to the destination, but avoid special
 * characters ('\n', '<' and '>') and remove crud at the end
 */
static int copy(char *buf, size_t size, int offset, const char *src)
{
	size_t i, len;
	unsigned char c;

	/* Remove crud from the beginning.. */
	while ((c = *src) != 0) {
		if (!crud(c))
			break;
		src++;
	}

	/* Remove crud from the end.. */
	len = strlen(src);
	while (len > 0) {
		c = src[len-1];
		if (!crud(c))
			break;
		--len;
	}

	/*
	 * Copy the rest to the buffer, but avoid the special
	 * characters '\n' '<' and '>' that act as delimiters on
	 * a identification line
	 */
	for (i = 0; i < len; i++) {
		c = *src++;
		switch (c) {
		case '\n': case '<': case '>':
			continue;
		}
		if (offset >= size)
			return size;
		buf[offset++] = c;
	}
	return offset;
}

static const char au_env[] = "GIT_AUTHOR_NAME";
static const char co_env[] = "GIT_COMMITTER_NAME";
static const char *env_hint =
"\n"
"*** Your name cannot be determined from your system services (gecos).\n"
"\n"
"Run\n"
"\n"
"  git config --global user.email \"you@email.com\"\n"
"  git config --global user.name \"Your Name\"\n"
"\n"
"to set your account\'s default identity.\n"
"Omit --global to set the identity only in this repository.\n"
"\n";

static const char *fmt_ident_1(const char *name, const char *email,
			       const char *date_str, int flag)
{
	static char buffer[1000];
	char date[50];
	int i;
	int error_on_no_name = !!(flag & 01);
	int name_addr_only = !!(flag & 02);

	setup_ident();
	if (!name)
		name = git_default_name;
	if (!email)
		email = git_default_email;

	if (!*name) {
		struct passwd *pw;

		if (0 <= error_on_no_name &&
		    name == git_default_name && env_hint) {
			fprintf(stderr, env_hint, au_env, co_env);
			env_hint = NULL; /* warn only once, for "git-var -l" */
		}
		if (0 < error_on_no_name)
			die("empty ident %s <%s> not allowed", name, email);
		pw = getpwuid(getuid());
		if (!pw)
			die("You don't exist. Go away!");
		strlcpy(git_default_name, pw->pw_name,
			sizeof(git_default_name));
		name = git_default_name;
	}

	strcpy(date, git_default_date);
	if (!name_addr_only && date_str)
		parse_date(date_str, date, sizeof(date));

	i = copy(buffer, sizeof(buffer), 0, name);
	i = add_raw(buffer, sizeof(buffer), i, " <");
	i = copy(buffer, sizeof(buffer), i, email);
	if (!name_addr_only) {
		i = add_raw(buffer, sizeof(buffer), i,  "> ");
		i = copy(buffer, sizeof(buffer), i, date);
	} else {
		i = add_raw(buffer, sizeof(buffer), i, ">");
	}
	if (i >= sizeof(buffer))
		die("Impossibly long personal identifier");
	buffer[i] = 0;
	return buffer;
}

const char *fmt_ident(const char *name, const char *email,
		      const char *date_str, int error_on_no_name)
{
	int flag = (error_on_no_name ? 01 : 0);
	return fmt_ident_1(name, email, date_str, flag);
}

const char *fmt_name(const char *name, const char *email)
{
	return fmt_ident_1(name, email, NULL, 03);
}

const char *git_author_info(int error_on_no_name)
{
	return fmt_ident(getenv("GIT_AUTHOR_NAME"),
			 getenv("GIT_AUTHOR_EMAIL"),
			 getenv("GIT_AUTHOR_DATE"),
			 error_on_no_name);
}

const char *git_committer_info(int error_on_no_name)
{
	return fmt_ident(getenv("GIT_COMMITTER_NAME"),
			 getenv("GIT_COMMITTER_EMAIL"),
			 getenv("GIT_COMMITTER_DATE"),
			 error_on_no_name);
}
