/*
 * libpurple-translate
 * Copyright (C) 2010  Eion Robb
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */
 
#define PURPLE_PLUGINS

#define VERSION "0.1"

#include <glib.h>
#include <string.h>

#include "util.h"
#include "plugin.h"
#include "debug.h"


typedef void(* TranslateCallback)(const gchar *original_phrase, const gchar *translated_phrase, const gchar *detected_language, gpointer userdata);
struct _TranslateStore {
	gchar *original_phrase;
	TranslateCallback callback;
	gpointer userdata;
};

void
google_translate_cb(PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message)
{
	struct _TranslateStore *store = user_data;
	const gchar *trans_start = "\"translatedText\":\"";
	const gchar *lang_start = "\"detectedSourceLanguage\":\"";
	gchar *strstart = NULL;
	gchar *translated = NULL;
	gchar *lang = NULL;

	purple_debug_info("translate", "Got response: %s\n", url_text);
	
	strstart = g_strstr_len(url_text, len, trans_start);
	if (strstart)
	{
		strstart = strstart + strlen(trans_start);
		translated = g_strndup(strstart, strchr(strstart, '"') - strstart);
	}
	
	strstart = g_strstr_len(url_text, len, lang_start);
	if (strstart)
	{
		strstart = strstart + strlen(lang_start);
		lang = g_strndup(strstart, strchr(strstart, '"') - strstart);
	}
	
	store->callback(store->original_phrase, translated, lang, store->userdata);
	
	g_free(translated);
	g_free(lang);
	g_free(store->original_phrase);
	g_free(store);
}

void
google_translate(const gchar *plain_phrase, const gchar *from_lang, const gchar *to_lang, TranslateCallback callback, gpointer userdata)
{
	gchar *encoded_phrase;
	gchar *url;
	struct _TranslateStore *store;
	
	encoded_phrase = g_strdup(purple_url_encode(plain_phrase));
	
	if (!from_lang || g_str_equal(from_lang, "auto"))
		from_lang = "";
	
	url = g_strdup_printf("http://ajax.googleapis.com/ajax/services/language/translate?v=1.0&langpair=%s%%7C%s&q=%s",
							from_lang, to_lang, encoded_phrase);
	
	store = g_new0(struct _TranslateStore, 1);
	store->original_phrase = g_strdup(plain_phrase);
	store->callback = callback;
	store->userdata = userdata;
	
	purple_debug_info("translate", "Fetching %s\n", url);
	
	purple_util_fetch_url_request(url, TRUE, "libpurple", FALSE, NULL, FALSE, google_translate_cb, store);
	
	g_free(encoded_phrase);
	g_free(url);
}

struct PurpleConvMessage {
	PurpleAccount *account;
	gchar *sender;
	PurpleConversation *conv;
	PurpleMessageFlags flags;
};

void
translate_receiving_message_cb(const gchar *original_phrase, const gchar *translated_phrase, const gchar *detected_language, gpointer userdata)
{
	struct PurpleConvMessage *convmsg = userdata;
	PurpleBuddy *buddy;
	gchar *html_text;
	
	if (detected_language)
	{
		buddy = purple_find_buddy(convmsg->account, convmsg->sender);
		purple_blist_node_set_string((PurpleBlistNode *)buddy, "eionrobb-translate-lang", detected_language);
	}
	
	html_text = purple_strdup_withhtml(translated_phrase);
	
	purple_conversation_write(convmsg->conv, convmsg->sender, html_text, convmsg->flags, time(NULL));
	
	g_free(html_text);
	g_free(convmsg->sender);
	g_free(convmsg);
}

gboolean
translate_receiving_im_msg(PurpleAccount *account, char **sender,
                             char **message, PurpleConversation *conv,
                             PurpleMessageFlags *flags)
{
	struct PurpleConvMessage *convmsg;
	const gchar *stored_lang = "";
	gchar *stripped;
	const gchar *to_lang;
	PurpleBuddy *buddy;
	
	convmsg = g_new0(struct PurpleConvMessage, 1);
	convmsg->account = account;
	convmsg->sender = *sender;
	convmsg->conv = conv;
	convmsg->flags = *flags;
	
	buddy = purple_find_buddy(account, *sender);
	//TODO check that we want to translate for this buddy
	if (0)
	{
		//Allow the message to go through as per normal
		return FALSE;
	}
	//stored_lang = purple_blist_node_get_string((PurpleBlistNode *)buddy, "eionrobb-translate-lang");
	
	stripped = purple_markup_strip_html(*message);
	to_lang = purple_prefs_get_string("/plugins/core/eionrobb-libpurple-translate/locale");
	
	google_translate(stripped, stored_lang, to_lang, translate_receiving_message_cb, convmsg);
	
	g_free(*message);
	*message = NULL;
	*sender = NULL;
	
	//Cancel the message
	return TRUE;
}

static void
init_plugin(PurplePlugin *plugin)
{
	purple_prefs_add_none("/plugins/core/eionrobb-libpurple-translate");
	purple_prefs_add_string("/plugins/core/eionrobb-libpurple-translate/locale", "en");
}

static gboolean
plugin_load(PurplePlugin *plugin)
{
	purple_signal_connect(purple_conversations_get_handle(),
	                      "receiving-im-msg", plugin,
	                      PURPLE_CALLBACK(translate_receiving_im_msg), NULL);
	return TRUE;
}

static gboolean
plugin_unload(PurplePlugin *plugin)
{
	purple_signal_disconnect(purple_conversations_get_handle(),
	                         "receiving-im-msg", plugin,
	                         PURPLE_CALLBACK(translate_receiving_im_msg));
	return TRUE;
}

static PurplePluginInfo info = {
    PURPLE_PLUGIN_MAGIC,
    2,
    2,
    PURPLE_PLUGIN_STANDARD,
    NULL,
    0,
    NULL,
    PURPLE_PRIORITY_DEFAULT,

    "eionrobb-libpurple-translate",
    "Auto Translate",
    VERSION,

    "Translate incoming/outgoing messages",
    "",
    "",
    "", /* URL */

    plugin_load,   /* load */
    plugin_unload, /* unload */
    NULL,          /* destroy */

    NULL,
    NULL,
    NULL,
    NULL
};

PURPLE_INIT_PLUGIN(translate, init_plugin, info);
