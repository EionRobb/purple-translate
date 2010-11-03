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

#define VERSION "1.1"
#define BING_APPID "0FFF5300CD157A2E748DFCCF6D67F8028E5B578D"

#include <glib.h>
#include <string.h>

#include "util.h"
#include "plugin.h"
#include "debug.h"

/** This is the list of languages we support, populated in plugin_init */
static GList *supported_languages = NULL;

typedef void(* TranslateCallback)(const gchar *original_phrase, const gchar *translated_phrase, const gchar *detected_language, gpointer userdata);
struct _TranslateStore {
	gchar *original_phrase;
	TranslateCallback callback;
	gpointer userdata;
	gchar *detected_language; //optional - needed for Bing
};

/** Converts unicode strings such as \003d into =
  * Code blatantly nicked from the Facebook plugin */
gchar *
convert_unicode(const gchar *input)
{
	gunichar unicode_char;
	gchar unicode_char_str[6];
	gint unicode_char_len;
	gchar *next_pos;
	gchar *input_string;
	gchar *output_string;

	if (input == NULL)
		return NULL;

	next_pos = input_string = g_strdup(input);

	while ((next_pos = strstr(next_pos, "\\u")))
	{
		/* grab the unicode */
		sscanf(next_pos, "\\u%4x", &unicode_char);
		/* turn it to a char* */
		unicode_char_len = g_unichar_to_utf8(unicode_char, unicode_char_str);
		/* shove it back into the string */
		g_memmove(next_pos, unicode_char_str, unicode_char_len);
		/* move all the data after the \u0000 along */
		g_stpcpy(next_pos + unicode_char_len, next_pos + 6);
	}

	output_string = g_strcompress(input_string);
	g_free(input_string);

	return output_string;
}

const gchar *
get_language_name(const gchar *language_key)
{
	GList *l;
	const gchar *language_name = NULL;
	PurpleKeyValuePair *pair = NULL;
	
	for(l = supported_languages; l; l = l->next)
	{
		pair = (PurpleKeyValuePair *) l->data;
		if (g_str_equal(pair->key, language_key))
		{
			language_name = pair->value;
			break;
		}
	}
	
	return language_name;
}

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
		
		strstart = convert_unicode(translated);
		g_free(translated);
		translated = strstart;
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

void
bing_translate_cb(PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message)
{
	struct _TranslateStore *store = user_data;
	gchar *translated = NULL;
	gchar *temp;

	purple_debug_info("translate", "Got response: %s\n", url_text);
	
	temp = strchr(url_text, '"') + 1; 
	temp = g_strndup(temp, len - (temp - url_text) - 1);
	
	translated = convert_unicode(temp);
	g_free(temp);
	
	store->callback(store->original_phrase, translated, store->detected_language, store->userdata);
	
	g_free(translated);
	g_free(store->detected_language);
	g_free(store->original_phrase);
	g_free(store);
}

void
bing_translate_autodetect_cb(PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message)
{
	struct _TranslateStore *store = user_data;
	gchar *from_lang = NULL;
	gchar *to_lang;
	gchar *encoded_phrase;
	gchar *url;
	
	purple_debug_info("translate", "Got response: %s\n", url_text);
	
	if (!url_text || !len || g_strstr_len(url_text, len, "\"\""))
	{
		// Unknown language
		store->callback(store->original_phrase, store->original_phrase, NULL, store->userdata);
		g_free(store->detected_language);
		g_free(store->original_phrase);
		g_free(store);
		
	} else {
	
		from_lang = strchr(url_text, '"') + 1; 
		from_lang = g_strndup(from_lang, len - (from_lang - url_text) - 1);
		
		to_lang = store->detected_language;
		store->detected_language = from_lang;
		
		// Same as bing_translate() but we've already made the _TranslateStore
		encoded_phrase = g_strescape(purple_url_encode(store->original_phrase), NULL);
		url = g_strdup_printf("http://api.microsofttranslator.com/V2/Ajax.svc/Translate?appId=" BING_APPID "&text=%%22%s%%22&from=%s&to=%s",
						encoded_phrase, from_lang, to_lang);
		purple_debug_info("translate", "Fetching %s\n", url);
		
		purple_util_fetch_url_request(url, TRUE, "libpurple", FALSE, NULL, FALSE, bing_translate_cb, store);
		
		g_free(to_lang);
		g_free(encoded_phrase);
		g_free(url);
	}
}

void
bing_translate(const gchar *plain_phrase, const gchar *from_lang, const gchar *to_lang, TranslateCallback callback, gpointer userdata)
{
	gchar *encoded_phrase;
	gchar *url;
	struct _TranslateStore *store;
	PurpleUtilFetchUrlCallback urlcallback;
	
	encoded_phrase = g_strescape(purple_url_encode(plain_phrase), NULL);
	
	store = g_new0(struct _TranslateStore, 1);
	store->original_phrase = g_strdup(plain_phrase);
	store->callback = callback;
	store->userdata = userdata;
	
	if (!from_lang || !(*from_lang) || g_str_equal(from_lang, "auto"))
	{
		url = g_strdup_printf("http://api.microsofttranslator.com/V2/Ajax.svc/Detect?appId=" BING_APPID "&text=%%22%s%%22",
						encoded_phrase);
		store->detected_language = g_strdup(to_lang);
		urlcallback = bing_translate_autodetect_cb;
	} else {
		url = g_strdup_printf("http://api.microsofttranslator.com/V2/Ajax.svc/Translate?appId=" BING_APPID "&text=%%22%s%%22&from=%s&to=%s",
						encoded_phrase, from_lang, to_lang);
		urlcallback = bing_translate_cb;
	}
	
	purple_debug_info("translate", "Fetching %s\n", url);
	
	purple_util_fetch_url_request(url, TRUE, "libpurple", FALSE, NULL, FALSE, urlcallback, store);
	
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
	const gchar *language_name = NULL;
	gchar *message;
	
	if (detected_language)
	{
		buddy = purple_find_buddy(convmsg->account, convmsg->sender);
		stored_lang = purple_blist_node_get_string((PurpleBlistNode *)buddy, "eionrobb-translate-lang");
		purple_blist_node_set_string((PurpleBlistNode *)buddy, "eionrobb-translate-lang", detected_language);
		
		language_name = get_language_name(detected_language);
		
		if (language_name != NULL)
		{
			message = g_strdup_printf("Now translating to %s (auto-detected)", language_name);
			purple_conversation_write(convmsg->conv, NULL, message, PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG, time(NULL));
			g_free(message);
		}
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
	const gchar *stored_lang = "auto";
	gchar *stripped;
	const gchar *to_lang;
	PurpleBuddy *buddy;
	const gchar *service_to_use = "";
	
	buddy = purple_find_buddy(account, *sender);
	service_to_use = purple_prefs_get_string("/plugins/core/eionrobb-libpurple-translate/service");
	to_lang = purple_prefs_get_string("/plugins/core/eionrobb-libpurple-translate/locale");
	if (buddy)
		stored_lang = purple_blist_node_get_string((PurpleBlistNode *)buddy, "eionrobb-translate-lang");
	if (!stored_lang)
		stored_lang = "auto";
	if (!buddy || !service_to_use || g_str_equal(stored_lang, "none") || g_str_equal(stored_lang, to_lang))
	{
		//Allow the message to go through as per normal
		return FALSE;
	}
	
	if (conv == NULL)
		conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, *sender);
	
	stripped = purple_markup_strip_html(*message);
	
	convmsg = g_new0(struct TranslateConvMessage, 1);
	convmsg->account = account;
	convmsg->sender = *sender;
	convmsg->conv = conv;
	convmsg->flags = *flags;
	
	if (g_str_equal(service_to_use, "google"))
	{
		google_translate(stripped, stored_lang, to_lang, translate_receiving_message_cb, convmsg);
	} else if (g_str_equal(service_to_use, "bing"))
	{
		bing_translate(stripped, stored_lang, to_lang, translate_receiving_message_cb, convmsg);
	}
	
	g_free(stripped);
	
	g_free(*message);
	*message = NULL;
	*sender = NULL;
	
	//Cancel the message
	return TRUE;
}


void
translate_receiving_chat_msg_cb(const gchar *original_phrase, const gchar *translated_phrase, const gchar *detected_language, gpointer userdata)
{
	struct TranslateConvMessage *convmsg = userdata;
	PurpleChat *chat;
	gchar *html_text;
	const gchar *stored_lang = "";
	const gchar *language_name = NULL;
	gchar *message;
	
	if (detected_language)
	{
		chat = purple_blist_find_chat(convmsg->account, convmsg->conv->name);
		stored_lang = purple_blist_node_get_string((PurpleBlistNode *)chat, "eionrobb-translate-lang");
		purple_blist_node_set_string((PurpleBlistNode *)chat, "eionrobb-translate-lang", detected_language);
		
		language_name = get_language_name(detected_language);
		
		if (language_name != NULL)
		{
			message = g_strdup_printf("Now translating to %s (auto-detected)", language_name);
			purple_conversation_write(convmsg->conv, NULL, message, PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG, time(NULL));
			g_free(message);
		}
	}
	
	html_text = purple_strdup_withhtml(translated_phrase);
	
	purple_conversation_write(convmsg->conv, convmsg->sender, html_text, convmsg->flags, time(NULL));
	
	g_free(html_text);
	g_free(convmsg->sender);
	g_free(convmsg);
}

gboolean
translate_receiving_chat_msg(PurpleAccount *account, char **sender,
                             char **message, PurpleConversation *conv,
                             PurpleMessageFlags *flags)
{
	struct TranslateConvMessage *convmsg;
	const gchar *stored_lang = "auto";
	gchar *stripped;
	const gchar *to_lang;
	PurpleChat *chat;
	const gchar *service_to_use = "";
	
	chat = purple_blist_find_chat(account, conv->name);
	service_to_use = purple_prefs_get_string("/plugins/core/eionrobb-libpurple-translate/service");
	to_lang = purple_prefs_get_string("/plugins/core/eionrobb-libpurple-translate/locale");
	if (chat)
		stored_lang = purple_blist_node_get_string((PurpleBlistNode *)chat, "eionrobb-translate-lang");
	if (!stored_lang)
		stored_lang = "auto";
	if (!chat || !service_to_use || g_str_equal(stored_lang, "none") || g_str_equal(stored_lang, to_lang))
	{
		//Allow the message to go through as per normal
		return FALSE;
	}
	
	stripped = purple_markup_strip_html(*message);
	
	convmsg = g_new0(struct TranslateConvMessage, 1);
	convmsg->account = account;
	convmsg->sender = *sender;
	convmsg->conv = conv;
	convmsg->flags = *flags;
	
	if (g_str_equal(service_to_use, "google"))
	{
		google_translate(stripped, stored_lang, to_lang, translate_receiving_chat_msg_cb, convmsg);
	} else if (g_str_equal(service_to_use, "bing"))
	{
		bing_translate(stripped, stored_lang, to_lang, translate_receiving_chat_msg_cb, convmsg);
	}
	
	g_free(stripped);
	
	g_free(*message);
	*message = NULL;
	*sender = NULL;
	
	if (conv == NULL)
	{
		// Fake receiving a message to open the conversation window
		*message = g_strdup(" ");
		*flags |= PURPLE_MESSAGE_INVISIBLE | PURPLE_MESSAGE_NO_LOG;
		return FALSE;
	}
	
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
	
	if (!buddy || !service_to_use || !to_lang || g_str_equal(from_lang, to_lang) || g_str_equal(to_lang, "auto"))
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
	} else if (g_str_equal(service_to_use, "bing"))
	{
		bing_translate(stripped, from_lang, to_lang, translate_sending_message_cb, convmsg);
	}
	
	g_free(stripped);
	
	g_free(*message);
	*message = NULL;
}

void
translate_sending_chat_message_cb(const gchar *original_phrase, const gchar *translated_phrase, const gchar *detected_language, gpointer userdata)
{
	struct TranslateConvMessage *convmsg = userdata;
	gchar *html_text;
	int err = 0;
	
	html_text = purple_strdup_withhtml(translated_phrase);
	err = serv_chat_send(purple_account_get_connection(convmsg->account), purple_conv_chat_get_id(PURPLE_CONV_CHAT(convmsg->conv)), html_text, convmsg->flags);
	g_free(html_text);
	
	html_text = purple_strdup_withhtml(original_phrase);
	//if (err > 0)
	//{
	//	purple_conversation_write(convmsg->conv, convmsg->sender, html_text, convmsg->flags, time(NULL));
	//}
	
	purple_signal_emit(purple_conversations_get_handle(), "sent-chat-msg",
						convmsg->account, html_text,
						purple_conv_chat_get_id(PURPLE_CONV_CHAT(convmsg->conv)));
	
	g_free(html_text);
	g_free(convmsg->sender);
	g_free(convmsg);
}

void
translate_sending_chat_msg(PurpleAccount *account, char **message, int chat_id)
{
	const gchar *from_lang = "";
	const gchar *service_to_use = "";
	const gchar *to_lang = "";
	PurpleChat *chat = NULL;
	PurpleConversation *conv;
	struct TranslateConvMessage *convmsg;
	gchar *stripped;

	from_lang = purple_prefs_get_string("/plugins/core/eionrobb-libpurple-translate/locale");
	service_to_use = purple_prefs_get_string("/plugins/core/eionrobb-libpurple-translate/service");
	conv = purple_find_chat(purple_account_get_connection(account), chat_id);
	if (conv)
		chat = purple_blist_find_chat(account, conv->name);
	if (chat)
		to_lang = purple_blist_node_get_string((PurpleBlistNode *)chat, "eionrobb-translate-lang");
	
	if (!chat || !service_to_use || !to_lang || g_str_equal(from_lang, to_lang) || g_str_equal(to_lang, "auto"))
	{
		// Don't translate this message
		return;
	}
	
	stripped = purple_markup_strip_html(*message);
	
	convmsg = g_new0(struct TranslateConvMessage, 1);
	convmsg->account = account;
	convmsg->conv = conv;
	convmsg->flags = PURPLE_MESSAGE_SEND;
	
	if (g_str_equal(service_to_use, "google"))
	{
		google_translate(stripped, from_lang, to_lang, translate_sending_chat_message_cb, convmsg);
	} else if (g_str_equal(service_to_use, "bing"))
	{
		bing_translate(stripped, from_lang, to_lang, translate_sending_chat_message_cb, convmsg);
	}
	
	g_free(stripped);
	
	g_free(*message);
	*message = NULL;
}

static void
translate_action_blist_cb(PurpleBlistNode *node, PurpleKeyValuePair *pair)
{
	PurpleConversation *conv = NULL;
	gchar *message;
	PurpleChat *chat;
	PurpleContact *contact;
	PurpleBuddy *buddy;

	if (pair == NULL)
		purple_blist_node_set_string(node, "eionrobb-translate-lang", NULL);
	else
		purple_blist_node_set_string(node, "eionrobb-translate-lang", pair->key);
	
	switch(node->type)
	{
		case PURPLE_BLIST_CHAT_NODE:
			chat = (PurpleChat *) node;
			conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT,
							purple_chat_get_name(chat),
							chat->account);
			break;
		case PURPLE_BLIST_CONTACT_NODE:
			contact = (PurpleContact *) node;
			node = (PurpleBlistNode *)purple_contact_get_priority_buddy(contact);
			//fallthrough intentional
		case PURPLE_BLIST_BUDDY_NODE:
			buddy = (PurpleBuddy *) node;
			conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM,
							purple_buddy_get_name(buddy),
							purple_buddy_get_account(buddy));
			break;
			
		default:
			break;
	}
	
	if (conv != NULL && pair != NULL)
	{
		message = g_strdup_printf("Now translating to %s", (const gchar *)pair->value);
		purple_conversation_write(conv, NULL, message, PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG, time(NULL));
		g_free(message);
	}
}

static void
translate_extended_menu(PurpleBlistNode *node, GList **menu, PurpleCallback callback)
{
	const gchar *stored_lang;
	GList *menu_children = NULL;
	PurpleMenuAction *action;
	PurpleKeyValuePair *pair;
	GList *l;
	
	if (!node)
		return;
	
	stored_lang = purple_blist_node_get_string(node, "eionrobb-translate-lang");
	if (!stored_lang)
		stored_lang = "auto";

	action = purple_menu_action_new("Auto", callback, NULL, NULL);
	menu_children = g_list_append(menu_children, action);
	
	// Spacer
	menu_children = g_list_append(menu_children, NULL);
	
	for(l = supported_languages; l; l = l->next)
	{
		pair = (PurpleKeyValuePair *) l->data;
		action = purple_menu_action_new(pair->value, callback, pair, NULL);
		menu_children = g_list_append(menu_children, action);
	}
	
	// Create the menu for the languages
	action = purple_menu_action_new("Translate to...", NULL, NULL, menu_children);
	*menu = g_list_append(*menu, action);
}

static void
translate_blist_extended_menu(PurpleBlistNode *node, GList **menu)
{
	translate_extended_menu(node, menu, (PurpleCallback)translate_action_blist_cb);
}

static void
translate_action_conv_cb(PurpleConversation *conv, PurpleKeyValuePair *pair)
{
	PurpleBlistNode *node = NULL;
	gchar *message;
	
	if (conv->type == PURPLE_CONV_TYPE_IM)
		node = (PurpleBlistNode *) purple_find_buddy(conv->account, conv->name);
	else if (conv->type == PURPLE_CONV_TYPE_CHAT)
		node = (PurpleBlistNode *) purple_blist_find_chat(conv->account, conv->name);
	
	if (node != NULL)
	{
		translate_action_blist_cb(node, pair);
		
		if (pair != NULL)
		{
			message = g_strdup_printf("Now translating to %s", (const gchar *)pair->value);
			purple_conversation_write(conv, NULL, message, PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG, time(NULL));
			g_free(message);
		}
	}
}

static void
translate_conversation_created(PurpleConversation *conv)
{
	PurpleBlistNode *node = NULL;
	gchar *message;
	const gchar *language_key;
	const gchar *language_name;
	
	if (conv->type == PURPLE_CONV_TYPE_IM)
		node = (PurpleBlistNode *) purple_find_buddy(conv->account, conv->name);
	else if (conv->type == PURPLE_CONV_TYPE_CHAT)
		node = (PurpleBlistNode *) purple_blist_find_chat(conv->account, conv->name);
	
	if (node != NULL)
	{
		language_key = purple_blist_node_get_string(node, "eionrobb-translate-lang");
		
		if (language_key != NULL)
		{
			language_name = get_language_name(language_key);
		
			message = g_strdup_printf("Now translating to %s", language_name);
			purple_conversation_write(conv, NULL, message, PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_NO_LOG, time(NULL));
			g_free(message);
		}
	}
}

static void
translate_conv_extended_menu(PurpleConversation *conv, GList **menu)
{
	PurpleBlistNode *node = NULL;
	
	if (conv->type == PURPLE_CONV_TYPE_IM)
		node = (PurpleBlistNode *) purple_find_buddy(conv->account, conv->name);
	else if (conv->type == PURPLE_CONV_TYPE_CHAT)
		node = (PurpleBlistNode *) purple_blist_find_chat(conv->account, conv->name);
	
	if (node != NULL)
		translate_extended_menu(node, menu, (PurpleCallback)translate_action_conv_cb);
}

static PurplePluginPrefFrame *
plugin_config_frame(PurplePlugin *plugin)
{
	PurplePluginPrefFrame *frame;
	PurplePluginPref *ppref;
	GList *l = NULL;
	PurpleKeyValuePair *pair;
	
	frame = purple_plugin_pref_frame_new();
	
	ppref = purple_plugin_pref_new_with_name_and_label(
		"/plugins/core/eionrobb-libpurple-translate/locale",
		"My language:");
	purple_plugin_pref_set_type(ppref, PURPLE_PLUGIN_PREF_CHOICE);
	
	for(l = supported_languages; l; l = l->next)
	{
		pair = (PurpleKeyValuePair *) l->data;
		purple_plugin_pref_add_choice(ppref, pair->value, pair->key);
	}
	
	purple_plugin_pref_frame_add(frame, ppref);
	
	
	ppref = purple_plugin_pref_new_with_name_and_label(
		"/plugins/core/eionrobb-libpurple-translate/service",
		"Use service:");
	purple_plugin_pref_set_type(ppref, PURPLE_PLUGIN_PREF_CHOICE);
	
	purple_plugin_pref_add_choice(ppref, "Google Translate", "google");
	purple_plugin_pref_add_choice(ppref, "Microsoft Translator", "bing");
	
	purple_plugin_pref_frame_add(frame, ppref);
	
	return frame;
}

static void
init_plugin(PurplePlugin *plugin)
{
	const gchar * const * languages;
	languages = g_get_language_names();
	const gchar *language;
	guint i = 0;
	PurpleKeyValuePair *pair;
	
	while((language = languages[i++]))
		if (language && strlen(language) == 2)
			break;
	if (!language || strlen(language) != 2)
		language = "en";
	
	purple_prefs_add_none("/plugins/core/eionrobb-libpurple-translate");
	purple_prefs_add_string("/plugins/core/eionrobb-libpurple-translate/locale", language);
	purple_prefs_add_string("/plugins/core/eionrobb-libpurple-translate/service", "google");
	
#define add_language(label, code) \
	pair = g_new0(PurpleKeyValuePair, 1); \
	pair->key = g_strdup(code); \
	pair->value = g_strdup(label); \
	supported_languages = g_list_append(supported_languages, pair);
	
	add_language("Afrikaans", "af");
	add_language("Albanian", "sq");
	add_language("Arabic", "ar");
	add_language("Armenian", "hy");
	add_language("Azerbaijani", "az");
	add_language("Basque", "eu");
	add_language("Belarusian", "be");
	add_language("Bulgarian", "bg");
	add_language("Catalan", "ca");
	add_language("Chinese (Simplified)", "zh-CN");
	add_language("Chinese (Traditional)", "zh-TW");
	add_language("Croatian", "hr");
	add_language("Czech", "cs");
	add_language("Danish", "da");
	add_language("Dutch", "nl");
	add_language("English", "en");
	add_language("Estonian", "et");
	add_language("Filipino", "tl");
	add_language("Finnish", "fi");
	add_language("French", "fr");
	add_language("Galician", "gl");
	add_language("Georgian", "ka");
	add_language("German", "de");
	add_language("Greek", "el");
	add_language("Haitian Creole", "ht");
	add_language("Hebrew", "iw");
	add_language("Hindi", "hi");
	add_language("Hungarian", "hu");
	add_language("Icelandic", "is");
	add_language("Indonesian", "id");
	add_language("Irish", "ga");
	add_language("Italian", "it");
	add_language("Japanese", "ja");
	add_language("Korean", "ko");
	add_language("Latin", "la");
	add_language("Latvian", "lv");
	add_language("Lithuanian", "lt");
	add_language("Macedonian", "mk");
	add_language("Malay", "ms");
	add_language("Maltese", "mt");
	add_language("Norwegian", "no");
	add_language("Persian", "fa");
	add_language("Polish", "pl");
	add_language("Portuguese", "pt");
	add_language("Romanian", "ro");
	add_language("Russian", "ru");
	add_language("Serbian", "sr");
	add_language("Slovak", "sk");
	add_language("Slovenian", "sl");
	add_language("Spanish", "es");
	add_language("Swahili", "sw");
	add_language("Swedish", "sv");
	add_language("Thai", "th");
	add_language("Turkish", "tr");
	add_language("Ukrainian", "uk");
	add_language("Urdu", "ur");
	add_language("Vietnamese", "vi");
	add_language("Welsh", "cy");
	add_language("Yiddish", "yi");
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
	purple_signal_connect(purple_blist_get_handle(),
						  "blist-node-extended-menu", plugin,
						  PURPLE_CALLBACK(translate_blist_extended_menu), NULL);
	purple_signal_connect(purple_conversations_get_handle(),
						  "blist-node-extended-menu", plugin,
						  PURPLE_CALLBACK(translate_conv_extended_menu), NULL);
	purple_signal_connect(purple_conversations_get_handle(),
						  "conversation-created", plugin,
						  PURPLE_CALLBACK(translate_conversation_created), NULL);
	purple_signal_connect(purple_conversations_get_handle(),
	                      "receiving-chat-msg", plugin,
	                      PURPLE_CALLBACK(translate_receiving_chat_msg), NULL);
	purple_signal_connect(purple_conversations_get_handle(),
	                      "sending-chat-msg", plugin,
	                      PURPLE_CALLBACK(translate_sending_chat_msg), NULL);
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
	purple_signal_disconnect(purple_blist_get_handle(),
							 "blist-node-extended-menu", plugin,
							 PURPLE_CALLBACK(translate_blist_extended_menu));
	purple_signal_disconnect(purple_conversations_get_handle(),
							 "blist-node-extended-menu", plugin,
							 PURPLE_CALLBACK(translate_conv_extended_menu));
	purple_signal_disconnect(purple_conversations_get_handle(),
							 "conversation-created", plugin,
							 PURPLE_CALLBACK(translate_conversation_created));
	purple_signal_disconnect(purple_conversations_get_handle(),
	                         "receiving-chat-msg", plugin,
	                         PURPLE_CALLBACK(translate_receiving_chat_msg));
	purple_signal_disconnect(purple_conversations_get_handle(),
	                         "sending-chat-msg", plugin,
	                         PURPLE_CALLBACK(translate_sending_chat_msg));
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
    3,
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
