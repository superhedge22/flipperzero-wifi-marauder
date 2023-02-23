#include "../wifi_marauder_app_i.h"

void wifi_marauder_print_message(uint8_t* buf, size_t len, WifiMarauderApp* app) {
    // If text box store gets too big, then truncate it
    app->text_box_store_strlen += len;
    if(app->text_box_store_strlen >= WIFI_MARAUDER_TEXT_BOX_STORE_SIZE - 1) {
        furi_string_right(app->text_box_store, app->text_box_store_strlen / 2);
        app->text_box_store_strlen = furi_string_size(app->text_box_store) + len;
    }
    // Null-terminate buf and append to text box store
    buf[len] = '\0';
    furi_string_cat_printf(app->text_box_store, "%s", buf);
}

void wifi_marauder_get_prefix_from_cmd(char* dest, const char* command) {
    int start, end, delta;
    start = strlen("sniff");
    end = strcspn(command, " ");
    delta = end - start;
    strncpy(dest, command + start, end - start);
    dest[delta] = '\0';    
}

void wifi_marauder_create_pcap_file(WifiMarauderApp* app) {
    char prefix[10];
    char capture_file_path[100];
    wifi_marauder_get_prefix_from_cmd(prefix, app->selected_tx_string);

    app->capture_file = storage_file_alloc(app->storage);
    int i=0;
    do{
        snprintf(capture_file_path, sizeof(capture_file_path), "%s/%s_%d.pcap", MARAUDER_APP_FOLDER, prefix, i);
        i++;
    } while(storage_file_exists(app->storage, capture_file_path));

    if (!storage_file_open(app->capture_file, capture_file_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        dialog_message_show_storage_error(app->dialogs, "Cannot open pcap file");
    }
}

void wifi_marauder_console_output_handle_rx_data_cb(uint8_t* buf, size_t len, void* context) {
    furi_assert(context);
    WifiMarauderApp* app = context;

    // If it is a sniff function, open the pcap file for recording
    if (strncmp("sniff", app->selected_tx_string, strlen("sniff")) == 0 && !app->is_writing) {
        app->is_writing = true;
        if (!app->capture_file || !storage_file_is_open(app->capture_file)) {
            wifi_marauder_create_pcap_file(app);
        }
    }

    if (app->is_writing) {
        storage_file_write(app->capture_file, buf, len);
    } else {
        wifi_marauder_print_message(buf, len, app);
    }

    view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventRefreshConsoleOutput);
}

void wifi_marauder_scene_console_output_on_enter(void* context) {
    WifiMarauderApp* app = context;

    TextBox* text_box = app->text_box;
    text_box_reset(app->text_box);
    text_box_set_font(text_box, TextBoxFontText);
    if(app->focus_console_start) {
        text_box_set_focus(text_box, TextBoxFocusStart);
    } else {
        text_box_set_focus(text_box, TextBoxFocusEnd);
    }
    if(app->is_command) {
        furi_string_reset(app->text_box_store);
        app->text_box_store_strlen = 0;
        if(0 == strncmp("help", app->selected_tx_string, strlen("help"))) {
            const char* help_msg =
                "Marauder companion " WIFI_MARAUDER_APP_VERSION "\nFor app support/feedback,\nreach out to me:\n@cococode#6011 (discord)\n0xchocolate (github)\n";
            furi_string_cat_str(app->text_box_store, help_msg);
            app->text_box_store_strlen += strlen(help_msg);
        }

        if(app->show_stopscan_tip) {
            const char* help_msg = "Press BACK to send stopscan\n";
            furi_string_cat_str(app->text_box_store, help_msg);
            app->text_box_store_strlen += strlen(help_msg);
        }
    }

    // Set starting text - for "View Log", this will just be what was already in the text box store
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text_box_store));

    scene_manager_set_scene_state(app->scene_manager, WifiMarauderSceneConsoleOutput, 0);
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewConsoleOutput);

    // Register callback to receive data
    wifi_marauder_uart_set_handle_rx_data_cb(
        app->uart, wifi_marauder_console_output_handle_rx_data_cb); // setup callback for rx thread

    // Send command with newline '\n'
    if(app->is_command && app->selected_tx_string) {
        wifi_marauder_uart_tx(
            (uint8_t*)(app->selected_tx_string), strlen(app->selected_tx_string));
        wifi_marauder_uart_tx((uint8_t*)("\n"), 1);
    }
}

bool wifi_marauder_scene_console_output_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;

    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        text_box_set_text(app->text_box, furi_string_get_cstr(app->text_box_store));
        consumed = true;
    } else if(event.type == SceneManagerEventTypeTick) {
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_console_output_on_exit(void* context) {
    WifiMarauderApp* app = context;

    // Unregister rx callback
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);

    // Automatically stop the scan when exiting view
    if(app->is_command) {
        wifi_marauder_uart_tx((uint8_t*)("stopscan\n"), strlen("stopscan\n"));
    }

    app->is_writing = false;
    if (app->capture_file && storage_file_is_open(app->capture_file)) {
        storage_file_close(app->capture_file);
        storage_file_free(app->capture_file);
    }

}
