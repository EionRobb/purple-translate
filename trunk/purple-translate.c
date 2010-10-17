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

struct TranslateConvMessage {
	PurpleAccount *account;
	gchar *sender;
	PurpleConversation *conv;
	PurpleMessageFlags flags;
};

void
translate_receiving_message_cb(const gchar *original_phrase, const gchar *translated_phrase, const gchar *detected_language, gpointer userdata)
{
	struct TranslateConvMessage *convmsg = userdata;
	PurpleBuddy *buddy;
	gchar *html_text;
	const gchar *stored_lang = "";
	
	if (detected_language)
	{
		buddy = purple_find_buddy(convmsg->account, convmsg->sender);
		stored_lang = purple_blist_node_get_string((PurpleBlistNode *)buddy, "eionrobb-translate-lang");
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
	struct TranslateConvMessage *convmsg;
	const gchar *stored_lang = "";
	gchar *stripped;
	const gchar *to_lang;
	PurpleBuddy *buddy;
	const gchar *service_to_use = "";
	
	buddy = purple_find_buddy(account, *sender);
	service_to_use = purple_prefs_get_string("/plugins/core/eionrobb-libpurple-translate/service");
	if (buddy)
		stored_lang = purple_blist_node_get_string((PurpleBlistNode *)buddy, "eionrobb-translate-lang");
	if (!buddy || !service_to_use || g_str_equal(stored_lang, "none"))
	{
		//Allow the message to go through as per normal
		return FALSE;
	}
	
	stripped = purple_markup_strip_html(*message);
	to_lang = purple_prefs_get_string("/plugins/core/eionrobb-libpurple-translate/locale");
	
	convmsg = g_new0(struct TranslateConvMessage, 1);
	convmsg->account = account;
	convmsg->sender = *sender;
	convmsg->conv = conv;
	convmsg->flags = *flags;
	
	if (g_str_equal(service_to_use, "google"))
	{
		google_translate(stripped, stored_lang, to_lang, translate_receiving_message_cb, convmsg);
	}
	
	g_free(stripped);
	
	g_free(*message);
	*message = NULL;
	*sender = NULL;
	
	//Cancel the message
	return TRUE;
}

void
translate_sending_message_cb(const gchar *original_phrase, const gchar *translated_phrase, const gchar *detected_language, gpointer userdata)
{
	struct TranslateConvMessage *convmsg = userdata;
	gchar *html_text;
	int err = 0;
	
	html_text = purple_strdup_withhtml(translated_phrase);
	err = serv_send_im(purple_account_get_connection(convmsg->account), convmsg->sender, html_text, convmsg->flags);
	g_free(html_text);
	
	html_text = purple_strdup_withhtml(original_phrase);
	if (err > 0)
	{
		purple_conversation_write(convmsg->conv, convmsg->sender, html_text, convmsg->flags, time(NULL));
	}
	
	purple_signal_emit(purple_conversations_get_handle(), "sent-im-msg",
						convmsg->account, convmsg->sender, html_text);
	
	g_free(html_text);
	g_free(convmsg->sender);
	g_free(convmsg);
}

void
translate_sending_im_msg(PurpleAccount *account, const char *receiver, char **message)
{
	const gchar *from_lang = "";
	const gchar *service_to_use = "";
	const gchar *to_lang = "";
	PurpleBuddy *buddy;
	struct TranslateConvMessage *convmsg;
	gchar *stripped;

	from_lang = purple_prefs_get_string("/plugins/core/eionrobb-libpurple-translate/locale");
	service_to_use = purple_prefs_get_string("/plugins/core/eionrobb-libpurple-translate/service");
	buddy = purple_find_buddy(account, receiver);
	if (buddy)
		to_lang = purple_blist_node_get_string((PurpleBlistNode *)buddy, "eionrobb-translate-lang");
	
	if (!buddy || !service_to_use || g_str_equal(from_lang, to_lang) || !to_lang || g_str_equal(to_lang, "auto"))
	{
		// Don't translate this message
		return;
	}
	
	stripped = purple_markup_strip_html(*message);
	
	convmsg = g_new0(struct TranslateConvMessage, 1);
	convmsg->account = account;
	convmsg->sender = g_strdup(receiver);
	convmsg->conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, receiver, account);
	convmsg->flags = PURPLE_MESSAGE_SEND;
	
	if (g_str_equal(service_to_use, "google"))
	{
		google_translate(stripped, from_lang, to_lang, translate_sending_message_cb, convmsg);
	}
	
	g_free(stripped);
	
	g_free(*message);
	*message = NULL;
}

static PurplePluginPrefFrame *
plugin_config_frame(PurplePlugin *plugin)
{
	PurplePluginPrefFrame *frame;
	PurplePluginPref *ppref;
	
	frame = purple_plugin_pref_frame_new();
	
	ppref = purple_plugin_pref_new_with_name_and_label(
		"/plugins/core/eionrobb-libpurple-translate/locale",
		"My language:");
	purple_plugin_pref_set_type(ppref, PURPLE_PLUGIN_PREF_CHOICE);
	
	purple_plugin_pref_add_choice(ppref, "Afrikaans", "af");
	purple_plugin_pref_add_choice(ppref, "Albanian", "sq");
	purple_plugin_pref_add_choice(ppref, "Arabic", "ar");
	purple_plugin_pref_add_choice(ppref, "Armenian", "hy");
	purple_plugin_pref_add_choice(ppref, "Azerbaijani", "az");
	purple_plugin_pref_add_choice(ppref, "Basque", "eu");
	purple_plugin_pref_add_choice(ppref, "Belarusian", "be");
	purple_plugin_pref_add_choice(ppref, "Bulgarian", "bg");
	purple_plugin_pref_add_choice(ppref, "Catalan", "ca");
	purple_plugin_pref_add_choice(ppref, "Chinese (Simplified)", "zh-CN");
	purple_plugin_pref_add_choice(ppref, "Chinese (Traditional)", "zh-TW");
	purple_plugin_pref_add_choice(ppref, "Croatian", "hr");
	purple_plugin_pref_add_choice(ppref, "Czech", "cs");
	purple_plugin_pref_add_choice(ppref, "Danish", "da");
	purple_plugin_pref_add_choice(ppref, "Dutch", "nl");
	purple_plugin_pref_add_choice(ppref, "English", "en");
	purple_plugin_pref_add_choice(ppref, "Estonian", "et");
	purple_plugin_pref_add_choice(ppref, "Filipino", "tl");
	purple_plugin_pref_add_choice(ppref, "Finnish", "fi");
	purple_plugin_pref_add_choice(ppref, "French", "fr");
	purple_plugin_pref_add_choice(ppref, "Galician", "gl");
	purple_plugin_pref_add_choice(ppref, "Georgian", "ka");
	purple_plugin_pref_add_choice(ppref, "German", "de");
	purple_plugin_pref_add_choice(ppref, "Greek", "el");
	purple_plugin_pref_add_choice(ppref, "Haitian Creole", "ht");
	purple_plugin_pref_add_choice(ppref, "Hebrew", "iw");
	purple_plugin_pref_add_choice(ppref, "Hindi", "hi");
	purple_plugin_pref_add_choice(ppref, "Hungarian", "hu");
	purple_plugin_pref_add_choice(ppref, "Icelandic", "is");
	purple_plugin_pref_add_choice(ppref, "Indonesian", "id");
	purple_plugin_pref_add_choice(ppref, "Irish", "ga");
	purple_plugin_pref_add_choice(ppref, "Italian", "it");
	purple_plugin_pref_add_choice(ppref, "Japanese", "ja");
	purple_plugin_pref_add_choice(ppref, "Korean", "ko");
	purple_plugin_pref_add_choice(ppref, "Latin", "la");
	purple_plugin_pref_add_choice(ppref, "Latvian", "lv");
	purple_plugin_pref_add_choice(ppref, "Lithuanian", "lt");
	purple_plugin_pref_add_choice(ppref, "Macedonian", "mk");
	purple_plugin_pref_add_choice(ppref, "Malay", "ms");
	purple_plugin_pref_add_choice(ppref, "Maltese", "mt");
	purple_plugin_pref_add_choice(ppref, "Norwegian", "no");
	purple_plugin_pref_add_choice(ppref, "Persian", "fa");
	purple_plugin_pref_add_choice(ppref, "Polish", "pl");
	purple_plugin_pref_add_choice(ppref, "Portuguese", "pt");
	purple_plugin_pref_add_choice(ppref, "Romanian", "ro");
	purple_plugin_pref_add_choice(ppref, "Russian", "ru");
	purple_plugin_pref_add_choice(ppref, "Serbian", "sr");
	purple_plugin_pref_add_choice(ppref, "Slovak", "sk");
	purple_plugin_pref_add_choice(ppref, "Slovenian", "sl");
	purple_plugin_pref_add_choice(ppref, "Spanish", "es");
	purple_plugin_pref_add_choice(ppref, "Swahili", "sw");
	purple_plugin_pref_add_choice(ppref, "Swedish", "sv");
	purple_plugin_pref_add_choice(ppref, "Thai", "th");
	purple_plugin_pref_add_choice(ppref, "Turkish", "tr");
	purple_plugin_pref_add_choice(ppref, "Ukrainian", "uk");
	purple_plugin_pref_add_choice(ppref, "Urdu", "ur");
	purple_plugin_pref_add_choice(ppref, "Vietnamese", "vi");
	purple_plugin_pref_add_choice(ppref, "Welsh", "cy");
	purple_plugin_pref_add_choice(ppref, "Yiddish", "yi");
	
	purple_plugin_pref_frame_add(frame, ppref);
	
	
	ppref = purple_plugin_pref_new_with_name_and_label(
		"/plugins/core/eionrobb-libpurple-translate/service",
		"Use service:");
	purple_plugin_pref_set_type(ppref, PURPLE_PLUGIN_PREF_CHOICE);
	
	purple_plugin_pref_add_choice(ppref, "Google Translate", "google");
	
	purple_plugin_pref_frame_add(frame, ppref);
	
	return frame;
}

static void
init_plugin(PurplePlugin *plugin)
{
	purple_prefs_add_none("/plugins/core/eionrobb-libpurple-translate");
	purple_prefs_add_string("/plugins/core/eionrobb-libpurple-translate/locale", "en");
	purple_prefs_add_string("/plugins/core/eionrobb-libpurple-translate/service", "google");
}

static gboolean
plugin_load(PurplePlugin *plugin)
{
	purple_signal_connect(purple_conversations_get_handle(),
	                      "receiving-im-msg", plugin,
	                      PURPLE_CALLBACK(translate_receiving_im_msg), NULL);
	purple_signal_connect(purple_conversations_get_handle(),
						  "sending-im-msg", plugin,
						  PURPLE_CALLBACK(translate_sending_im_msg), NULL);
	return TRUE;
}

static gboolean
plugin_unload(PurplePlugin *plugin)
{
	purple_signal_disconnect(purple_conversations_get_handle(),
	                         "receiving-im-msg", plugin,
	                         PURPLE_CALLBACK(translate_receiving_im_msg));
	purple_signal_disconnect(purple_conversations_get_handle(),
							 "sending-im-msg", plugin,
							 PURPLE_CALLBACK(translate_sending_im_msg));
	return TRUE;
}

static PurplePluginUiInfo prefs_info = {
	plugin_config_frame,
	0,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

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
    "Eion Robb <eionrobb@gmail.com>",
    "http://purple-translate.googlecode.com/", /* URL */

    plugin_load,   /* load */
    plugin_unload, /* unload */
    NULL,          /* destroy */

    NULL,
    NULL,
    &prefs_info,
    NULL
};

PURPLE_INIT_PLUGIN(translate, init_plugin, info);
