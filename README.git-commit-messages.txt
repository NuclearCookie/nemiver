Git commit messages format
==========================

The principle of the git commit log is to document at least the
*what*; in English.  That is redundant with the commit diff, yes.  But
that redundancy does help in understanding the commit diff better.  If
appropriate, the commit log can also document the *why*, but only if
it does so by respecting the format of the commit log.  The reason why
we are so strict about the format is that the commit log is later
parsed by a tool to build a ChangeLog, which we want to stay compliant
with the GNU ChangeLog format.

So here is the format we are talking about.

The first line of a git commit message should start at column 1, with
no space. That line should be a short summary of the purpose of the
commit.  If the commit relates to a bug filed into bugzilla, the line
should begin with the bug number, followed by a white space; e.g:

#<number-of-the-bug> This is a great commit

The line in its entirety should not be longer than 50 characters.

The next line should be an empty line, with no spaces.

The subsequent lines should have the form of the Body of a GNU ChangeLog
entry, i.e:

	* file1.c (func1): Changed foo in this function.
	(func2): Changed blah in that function
	* file2.c (func_foo): Changed something here.

Note that before the '*', there is a tab that is 8 spaces long.  Also
note that right after the '*', there is a space.

An exemple of commit message would be:

~=~
123456 Shorten compilation lines

	* configure.ac: Shorten compilation lines by regrouping
	PKG_CHECK_MODULES calls.
	* tests/Makefile.am: Adjust this.
~=~

We encourage you to look at the existing commit logs or at the
ChangeLog file for inspiration.
