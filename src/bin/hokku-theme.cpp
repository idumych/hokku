/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * hokku-theme: the themes of Appearance's Themes tab, from a shell.
 *
 *   hokku-theme list           the installed themes; * marks the current one
 *   hokku-theme apply NAME
 *   hokku-theme save NAME      the look in use now, as a theme of your own
 *   hokku-theme current
 *   hokku-theme shadows [on | off]   whether windows and the pointer cast a
 *                                    shadow (Appearance's Look and feel)
 */


#include <stdio.h>
#include <string.h>

#include <Application.h>

#include <DecoratorPrivate.h>

#include "../preferences/appearance/Theme.h"


static const char* kSignature = "application/x-vnd.Hokku-Theme";


static Theme*
find_theme(ThemeList& themes, const char* name)
{
	for (int32 i = 0; i < themes.CountItems(); i++) {
		if (themes.ItemAt(i)->name.ICompare(name) == 0)
			return themes.ItemAt(i);
	}
	return NULL;
}


static int
usage()
{
	fprintf(stderr, "usage: hokku-theme list | apply NAME | save NAME "
		"| current | shadows [on | off]\n");
	return 2;
}


int
main(int argc, char** argv)
{
	if (argc < 2)
		return usage();
	const char* command = argv[1];

	BApplication app(kSignature);
	ThemeList themes;
	find_themes(themes);

	if (strcmp(command, "list") == 0) {
		BString current = current_theme_name();
		for (int32 i = 0; i < themes.CountItems(); i++) {
			Theme* theme = themes.ItemAt(i);
			printf("%s %s%s\n", theme->name == current ? "*" : " ",
				theme->name.String(), theme->IsUserTheme() ? " (yours)" : "");
		}
		return 0;
	}

	if (strcmp(command, "current") == 0) {
		printf("%s\n", current_theme_name().String());
		return 0;
	}

	if (strcmp(command, "shadows") == 0) {
		if (argc == 2) {
			printf("%s\n", BPrivate::get_window_shadows() ? "on" : "off");
			return 0;
		}
		if (argc != 3 || (strcmp(argv[2], "on") != 0
				&& strcmp(argv[2], "off") != 0))
			return usage();
		if (apply_window_shadows(strcmp(argv[2], "on") == 0) != B_OK) {
			fprintf(stderr, "hokku-theme: can't change the shadows\n");
			return 1;
		}
		return 0;
	}

	if (argc != 3)
		return usage();
	const char* name = argv[2];

	if (strcmp(command, "apply") == 0) {
		Theme* theme = find_theme(themes, name);
		if (theme == NULL) {
			fprintf(stderr, "hokku-theme: no theme called \"%s\"\n", name);
			return 1;
		}
		theme->Apply();
		printf("%s applied. Applications that are already running pick up a "
			"new control look when restarted.\n", theme->name.String());
		return 0;
	}

	if (strcmp(command, "save") == 0) {
		if (strchr(name, '/') != NULL) {
			fprintf(stderr, "hokku-theme: a theme's name can't have a /\n");
			return 1;
		}
		Theme theme;
		theme.Capture(name);
		BPath folder;
		user_themes_folder(folder, true);
		folder.Append(name);
		status_t status = theme.Save(folder);
		if (status != B_OK) {
			fprintf(stderr, "hokku-theme: can't save %s: %s\n", folder.Path(),
				strerror(status));
			return 1;
		}
		set_current_theme_name(name);
		printf("Saved as %s\n", folder.Path());
		return 0;
	}

	return usage();
}
