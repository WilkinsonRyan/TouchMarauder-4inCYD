#include "MenuFunctions.h"
#include "lang_var.h"
//#include "icons.h"
#include "configs.h"
#include "TouchDrvGT911.hpp"
#include "esp_ota_ops.h"        // dual-boot: hand off to the TouchBoard app slot (ota_1)
#include "esp_partition.h"

// ▲▼ scroll-arrow geometry: a slim column on the right edge of the menu area.
// Defined here (before main()) so both the touch handler and the draw code see it.
#define SCROLL_ARROW_X 286
#define SCROLL_ARROW_W  30
#define SCROLL_ARROW_H  34
#define SCROLL_UP_Y     52
#define SCROLL_DN_Y    444
// Disabled-arrow grey, ~30% darker than TFT_DARKGREY (0x7BEF) for a clearer
// "can't scroll this way" state.
#define SCROLL_DISABLED_GREY 0x52CA

TouchDrvGT911 touch;



extern const unsigned char menu_icons[][66];
PROGMEM lv_obj_t * slider_label;
PROGMEM lv_obj_t * ta1;
PROGMEM lv_obj_t * ta2;
PROGMEM lv_obj_t * save_name;

MenuFunctions::MenuFunctions()
{
}

// LVGL Stuff
/* Interrupt driven periodic handler */
#if defined(HAS_ST7789) || defined(HAS_ILI9341) || defined(HAS_ST7796)
    #if defined(CYD_32CAP) || defined(CYD_35CAP)
      uint8_t MenuFunctions::updateTouch(int16_t *x, int16_t *y, uint16_t threshold) {
        static bool was_pressed = false;
        if (!display_obj.headless_mode) {
          int16_t t_x[5] = {0,0,0,0,0}, t_y[5] = {0,0,0,0,0};
          uint8_t result = touch.getPoint(t_x, t_y, touch.getSupportTouchPoint());
          bool pressed = result > 0;

          if (pressed && !was_pressed) {
            for (int i = 0; i < result; i++) {
              x[i] = t_x[i];
              y[i] = t_y[i];
            }
            
            int16_t tmp_x[5] = {0,0,0,0,0}, tmp_y[5] = {0,0,0,0,0};
            for (int i = 0; i < THROW_AWAY_TOUCH_COUNT; i++) {
              touch.getPoint(tmp_x, tmp_y, touch.getSupportTouchPoint());
              delay(1);
            }
            was_pressed = true;
            return result;
          } else if (!pressed) {
            was_pressed = false;
          }
          return 0;
        } else {
          Serial.println("headless mode");
          return !display_obj.headless_mode;
        }
      }
    #elif defined(CYD_24CAP) || defined(CYD_22CAP)
    uint8_t MenuFunctions::updateTouch(uint16_t *x, uint16_t *y, uint16_t threshold) {
      if (!display_obj.headless_mode)
        return display_obj.tft.getTouchBBC(x, y, threshold);
      else
        return !display_obj.headless_mode;
    }
    #else
      // Resistive touch
      uint8_t MenuFunctions::updateTouch(uint16_t *x, uint16_t *y, uint16_t threshold) {
        if (!display_obj.headless_mode)
          return display_obj.tft.getTouch(x, y, threshold);
        else
          return !display_obj.headless_mode;
      }
    #endif
  



  void MenuFunctions::lv_tick_handler()
  {
    lv_tick_inc(LVGL_TICK_PERIOD);
  }
  
  /* Display flushing */
  void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
  {
    extern Display display_obj;
    uint16_t c;
  
    display_obj.tft.startWrite();
    display_obj.tft.setAddrWindow(area->x1, area->y1, (area->x2 - area->x1 + 1), (area->y2 - area->y1 + 1));
    for (int y = area->y1; y <= area->y2; y++) {
      for (int x = area->x1; x <= area->x2; x++) {
        c = color_p->full;
        display_obj.tft.writeColor(c, 1);
        color_p++;
      }
    }
    display_obj.tft.endWrite();
    lv_disp_flush_ready(disp);
  }
  
  bool my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    extern Display display_obj;
    static bool was_pressed = false;
    bool touched = false;
  
    #if defined(CYD_32CAP) || defined(CYD_35CAP)    
      int16_t touchX = 0, touchY = 0;
    #else
      uint16_t touchX, touchY;
    #endif

  #if defined(CYD_32CAP) || defined(CYD_35CAP)
      touch.setMaxCoordinates(SCREEN_HEIGHT, SCREEN_WIDTH);
      touch.setSwapXY(true);
      touch.setMirrorXY(true, true);
      int16_t touchX_array[5] = {0, 0, 0, 0, 0}, touchY_array[5] = {0, 0, 0, 0, 0};
      int16_t points = touch.getPoint(touchX_array, touchY_array, touch.getSupportTouchPoint());
      touched = (points > 0);
      if (touched) {
          touchX = touchX_array[0];
          touchY = touchY_array[0];
          if (!was_pressed) {
              for (int i = 0; i < THROW_AWAY_TOUCH_COUNT; i++) {
                  int16_t tmp_x[5] = {0, 0, 0, 0, 0}, tmp_y[5] = {0, 0, 0, 0, 0};
                  touch.getPoint(tmp_x, tmp_y, touch.getSupportTouchPoint());
                  delay(1);
              }
          }
      }
      was_pressed = touched;
  #elif defined(CYD_24CAP)
      touched = display_obj.tft.getTouchBBC(&touchX, &touchY, 600);
  #else
      touched = display_obj.tft.getTouch(&touchX, &touchY, 600);
  #endif

  // Handle touch state and coordinates
  if (!touched) {
      data->state = LV_INDEV_STATE_REL;
      return false;
  }
  if (touchX >= WIDTH_1 || touchY >= HEIGHT_1) {
      Serial.println("Touch outside expected parameters:");
      Serial.print("x: ");
      Serial.print(touchX);
      Serial.print(" y: ");
      Serial.print(touchY);
      data->state = LV_INDEV_STATE_REL;
      return false;
  }

  data->state = LV_INDEV_STATE_PR;
  data->point.x = touchX;
  data->point.y = touchY;
  return false;
}
    

  void MenuFunctions::initLVGL() {
    tick.attach_ms(LVGL_TICK_PERIOD, lv_tick_handler);
    lv_init();
  
    lv_disp_buf_init(&disp_buf, buf, NULL, LV_HOR_RES_MAX * 10);
  
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = WIDTH_1;
    disp_drv.ver_res = HEIGHT_1;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);
  
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);             
    indev_drv.type = LV_INDEV_TYPE_POINTER;    
    indev_drv.read_cb = my_touchpad_read;      
    lv_indev_drv_register(&indev_drv);

  //#if defined(CYD_32CAP) || defined(CYD_35CAP)
  //  touch.setMaxCoordinates(SCREEN_WIDTH, SCREEN_HEIGHT);
  //  touch.setSwapXY(false);
  //  touch.setMirrorXY(false, false);
  //#endif
      //display_obj.exit_draw = true;         
  }
  
  
  void MenuFunctions::deinitLVGL() {
      //Serial.println(F("Deinit LVGL"));
      #if defined(CYD_32CAP) || defined(CYD_35CAP)
        touch.setMaxCoordinates(SCREEN_WIDTH, SCREEN_HEIGHT);
        touch.setSwapXY(false);
        touch.setMirrorXY(false, false);
      #endif
      //display_obj.exit_draw = true;
  }
  
  
  // Event handler for settings drop down menus
  void setting_dropdown_cb(lv_obj_t * obj, lv_event_t event) {

  }

  // GFX Function to build a list showing all Stations scanned
  void MenuFunctions::addStationGFX(){
    extern LinkedList<Station>* stations;
    extern LinkedList<AccessPoint>* access_points;
    extern WiFiScan wifi_scan_obj;
  
    lv_obj_t * list1 = lv_list_create(lv_scr_act(), NULL);
    lv_obj_set_size(list1, 320, 240);
    lv_obj_set_width(list1, LV_HOR_RES);
    lv_obj_align(list1, NULL, LV_ALIGN_CENTER, 0, 0);
  
    lv_obj_t * list_btn;
  
    lv_obj_t * label;
  
    list_btn = lv_list_add_btn(list1, LV_SYMBOL_CLOSE, text09);
    lv_obj_set_event_cb(list_btn, station_list_cb);

    char addr[] = "00:00:00:00:00:00";
    for (int x = 0; x < access_points->size(); x++) {
      AccessPoint cur_ap = access_points->get(x);

      // Add non clickable button for AP
      String full_label = "AP: " + cur_ap.essid;
      char buf[full_label.length() + 1] = {};
      full_label.toCharArray(buf, full_label.length() + 1);
      list_btn = lv_list_add_btn(list1, NULL, buf);
      lv_btn_set_checkable(list_btn, false);
      
      int cur_ap_sta_len = access_points->get(x).stations->size();
      for (int y = 0; y < cur_ap_sta_len; y++) {
        Station cur_sta = stations->get(cur_ap.stations->get(y));
        // Convert uint8_t MAC to char array
        wifi_scan_obj.getMAC(addr, cur_sta.mac, 0);
        
        list_btn = lv_list_add_btn(list1, LV_SYMBOL_WIFI, addr);
        lv_btn_set_checkable(list_btn, true);
        lv_obj_set_event_cb(list_btn, station_list_cb);
    
        if (cur_sta.selected)
          lv_btn_toggle(list_btn);
      }
    }
  }

  // Function to work with list of Stations
  void station_list_cb(lv_obj_t * btn, lv_event_t event) {
    extern LinkedList<Station>* stations;
    extern MenuFunctions menu_function_obj;
    extern WiFiScan wifi_scan_obj;
  
    String btn_text = lv_list_get_btn_text(btn);
    String display_string = "";
    char addr[] = "00:00:00:00:00:00";
    
    if (event == LV_EVENT_CLICKED) {
      if (btn_text != text09) {
      }
      else {
        Serial.println("Exiting...");
        lv_obj_del_async(lv_obj_get_parent(lv_obj_get_parent(btn)));
  
        for (int i = 0; i < stations->size(); i++) {
          if (stations->get(i).selected) {
            wifi_scan_obj.getMAC(addr, stations->get(i).mac, 0);
            Serial.print("Selected: ");
            Serial.println(addr);
          }
        }
  
        printf("LV_EVENT_CANCEL\n");
        menu_function_obj.deinitLVGL();
        wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
        display_obj.exit_draw = true; // set everything back to normal
      }
    }
    
    if (event == LV_EVENT_VALUE_CHANGED) {     
      if (lv_btn_get_state(btn) == LV_BTN_STATE_CHECKED_RELEASED) {
        for (int i = 0; i < stations->size(); i++) {
          wifi_scan_obj.getMAC(addr, stations->get(i).mac, 0); 
          if (strcmp(addr, btn_text.c_str()) == 0) {
            Serial.print("Adding Station: ");
            Serial.println(addr);
            Station sta = stations->get(i);
            sta.selected = true;
            stations->set(i, sta);
          }
        }
      }
      else {
        for (int i = 0; i < stations->size(); i++) {
          wifi_scan_obj.getMAC(addr, stations->get(i).mac, 0); 
          if (strcmp(addr, btn_text.c_str()) == 0) {
            Serial.print("Removing Station: ");
            Serial.println(addr);
            Station sta = stations->get(i);
            sta.selected = false;
            stations->set(i, sta);
          }
        }
      }
    }
  }

  // GFX Function to build a list showing all EP HTML Files
  void MenuFunctions::selectEPHTMLGFX() {
    extern EvilPortal evil_portal_obj;
  
    lv_obj_t * list1 = lv_list_create(lv_scr_act(), NULL);
    lv_obj_set_size(list1, 160, 200);
    lv_obj_set_width(list1, LV_HOR_RES);
    lv_obj_align(list1, NULL, LV_ALIGN_CENTER, 0, 0);
  
    lv_obj_t * list_btn;
  
    lv_obj_t * label;
  
    list_btn = lv_list_add_btn(list1, LV_SYMBOL_CLOSE, text09);
    lv_obj_set_event_cb(list_btn, html_list_cb);
  
    for (int i = 1; i < evil_portal_obj.html_files->size(); i++) {
      char buf[evil_portal_obj.html_files->get(i).length() + 1] = {};
      evil_portal_obj.html_files->get(i).toCharArray(buf, evil_portal_obj.html_files->get(i).length() + 1);
      
      list_btn = lv_list_add_btn(list1, LV_SYMBOL_FILE, buf);
      lv_btn_set_checkable(list_btn, true);
      lv_obj_set_event_cb(list_btn, html_list_cb);
  
      if (i == evil_portal_obj.selected_html_index)
        lv_btn_toggle(list_btn);
    }
  }

  void html_list_cb(lv_obj_t * btn, lv_event_t event) {
    extern EvilPortal evil_portal_obj;
    extern MenuFunctions menu_function_obj;
  
    String btn_text = lv_list_get_btn_text(btn);
    String display_string = "";
    
    if (event == LV_EVENT_CLICKED) {
      if (btn_text != text09) {
      }
      else {
        Serial.println("Exiting...");
        lv_obj_del_async(lv_obj_get_parent(lv_obj_get_parent(btn)));
  
        for (int i = 1; i < evil_portal_obj.html_files->size(); i++) {
          if (i == evil_portal_obj.selected_html_index) {
            Serial.println("Selected: " + (String)evil_portal_obj.html_files->get(i));
          }
        }
  
        printf("LV_EVENT_CANCEL\n");
        menu_function_obj.deinitLVGL();
        wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
        display_obj.exit_draw = true; // set everything back to normal
      }
    }
    
    if (event == LV_EVENT_VALUE_CHANGED) {      
      if (lv_btn_get_state(btn) == LV_BTN_STATE_CHECKED_RELEASED) {
        for (int i = 1; i < evil_portal_obj.html_files->size(); i++) {
          if (evil_portal_obj.html_files->get(i) == btn_text) {
            Serial.println("Setting HTML: " + (String)evil_portal_obj.html_files->get(i));
            evil_portal_obj.selected_html_index = i;
            evil_portal_obj.target_html_name = (String)evil_portal_obj.html_files->get(i);
          }
        }

        // Deselect buttons that were previously selected
        lv_obj_t * list = lv_obj_get_parent(btn);

        lv_obj_t * next_btn = lv_obj_get_child(list, NULL);
        while (next_btn != NULL) {
          if (next_btn != btn) {
            lv_btn_set_state(next_btn, LV_BTN_STATE_RELEASED);
          }
          next_btn = lv_obj_get_child(list, next_btn);
        }
      }
    }
  }
  
  // GFX Function to build a list showing all APs scanned
  void MenuFunctions::addAPGFX(String type){
    extern WiFiScan wifi_scan_obj;
    extern LinkedList<AccessPoint>* access_points;
    extern LinkedList<AirTag>* airtags;
  
    lv_obj_t * list1 = lv_list_create(lv_scr_act(), NULL);
    lv_obj_set_size(list1, 160, 200);
    lv_obj_set_width(list1, LV_HOR_RES);
    lv_obj_align(list1, NULL, LV_ALIGN_CENTER, 0, 0);
  
    lv_obj_t * list_btn;
  
    lv_obj_t * label;
  
    list_btn = lv_list_add_btn(list1, LV_SYMBOL_CLOSE, text09);
    lv_obj_set_event_cb(list_btn, ap_list_cb);
  
    if ((type == "AP") || (type == "AP Info")) {
      for (int i = 0; i < access_points->size(); i++) {
        char buf[access_points->get(i).essid.length() + 1] = {};
        access_points->get(i).essid.toCharArray(buf, access_points->get(i).essid.length() + 1);
        
        list_btn = lv_list_add_btn(list1, LV_SYMBOL_WIFI, buf);
        lv_btn_set_checkable(list_btn, true);
        if (type == "AP")
          lv_obj_set_event_cb(list_btn, ap_list_cb);
        else if (type == "AP Info")
          lv_obj_set_event_cb(list_btn, ap_info_list_cb);
    
        if (access_points->get(i).selected)
          lv_btn_toggle(list_btn);
      }
    }
    else if (type == "Airtag") {
      for (int i = 0; i < airtags->size(); i++) {
        char buf[airtags->get(i).mac.length() + 1] = {};
        airtags->get(i).mac.toCharArray(buf, airtags->get(i).mac.length() + 1);
        
        list_btn = lv_list_add_btn(list1, LV_SYMBOL_BLUETOOTH, buf);
        lv_btn_set_checkable(list_btn, true);
        lv_obj_set_event_cb(list_btn, at_list_cb);
    
        //if (airtags->get(i).selected)
        //  lv_btn_toggle(list_btn);
      }
    }
  }
  
  void at_list_cb(lv_obj_t * btn, lv_event_t event) {
    extern MenuFunctions menu_function_obj;
    extern WiFiScan wifi_scan_obj;
    extern LinkedList<AirTag>* airtags;
    extern Display display_obj;
  
    String btn_text = lv_list_get_btn_text(btn);
    String display_string = "";
    
    // Button is clicked
    if (event == LV_EVENT_CLICKED) {
      if (btn_text != text09) {
      }
      // It's the back button
      else {
        Serial.println("Exiting...");
        lv_obj_del_async(lv_obj_get_parent(lv_obj_get_parent(btn)));
  
        for (int i = 0; i < airtags->size(); i++) {
          if (airtags->get(i).selected) {
            Serial.println("Selected: " + (String)airtags->get(i).mac);
          }
        }
  
        printf("LV_EVENT_CANCEL\n");
        menu_function_obj.deinitLVGL();
        wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
        display_obj.exit_draw = true; // set everything back to normal
      }
    }
    
    if (event == LV_EVENT_VALUE_CHANGED) {      
      if (lv_btn_get_state(btn) == LV_BTN_STATE_CHECKED_RELEASED) {
        bool do_that_thang = false;
        for (int i = 0; i < airtags->size(); i++) {
          if (airtags->get(i).mac == btn_text) {
            Serial.println("Selecting Airtag: " + (String)airtags->get(i).mac);
            AirTag at = airtags->get(i);
            at.selected = true;
            airtags->set(i, at);
            do_that_thang = true;
          }
          else {
            AirTag at = airtags->get(i);
            at.selected = false;
            airtags->set(i, at);
          }
        }
        // Start spoofing airtag
        if (do_that_thang) {
          menu_function_obj.deinitLVGL();
          lv_obj_del_async(lv_obj_get_parent(lv_obj_get_parent(btn)));
          wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
          display_obj.clearScreen();
          menu_function_obj.orientDisplay();
          display_obj.clearScreen();
          menu_function_obj.drawStatusBar();
          wifi_scan_obj.StartScan(BT_SPOOF_AIRTAG, TFT_WHITE);
        }
      }
      else {
        for (int i = 0; i < airtags->size(); i++) {
          if (airtags->get(i).mac == btn_text) {
            Serial.println("Deselecting Airtag: " + (String)airtags->get(i).mac);
            AirTag at = airtags->get(i);
            at.selected = false;
            airtags->set(i, at);
          }
        }
      }
    }
  }
  
  void ap_list_cb(lv_obj_t * btn, lv_event_t event) {
    extern LinkedList<AccessPoint>* access_points;
    extern MenuFunctions menu_function_obj;
    extern WiFiScan wifi_scan_obj;
  
    String btn_text = lv_list_get_btn_text(btn);
    String display_string = "";
    
    if (event == LV_EVENT_CLICKED) {
      if (btn_text != text09) {
      }
      else {
        Serial.println("Exiting...");
        lv_obj_del_async(lv_obj_get_parent(lv_obj_get_parent(btn)));
  
        for (int i = 0; i < access_points->size(); i++) {
          if (access_points->get(i).selected) {
            Serial.println("Selected: " + (String)access_points->get(i).essid);
          }
        }
  
        printf("LV_EVENT_CANCEL\n");
        menu_function_obj.deinitLVGL();
        wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
        display_obj.exit_draw = true; // set everything back to normal
      }
    }
    
    if (event == LV_EVENT_VALUE_CHANGED) {      
      if (lv_btn_get_state(btn) == LV_BTN_STATE_CHECKED_RELEASED) {
        for (int i = 0; i < access_points->size(); i++) {
          if (access_points->get(i).essid == btn_text) {
            Serial.println("Adding AP: " + (String)access_points->get(i).essid);
            AccessPoint ap = access_points->get(i);
            ap.selected = true;
            access_points->set(i, ap);
          }
        }
      }
      else {
        for (int i = 0; i < access_points->size(); i++) {
          if (access_points->get(i).essid == btn_text) {
            Serial.println("Removing AP: " + (String)access_points->get(i).essid);
            AccessPoint ap = access_points->get(i);
            ap.selected = false;
            access_points->set(i, ap);
          }
        }
      }
    }
  }
  
  void ap_info_list_cb(lv_obj_t * btn, lv_event_t event) {
    extern LinkedList<AccessPoint>* access_points;
    extern MenuFunctions menu_function_obj;
    extern WiFiScan wifi_scan_obj;
  
    String btn_text = lv_list_get_btn_text(btn);
    String display_string = "";
    
    // Exit function
    if (event == LV_EVENT_CLICKED) {
      if (btn_text != text09) {
        for (int i = 0; i < access_points->size(); i++) {
          if (access_points->get(i).essid == btn_text) {
            lv_obj_del_async(lv_obj_get_parent(lv_obj_get_parent(btn)));
  
            printf("LV_EVENT_CANCEL\n");
            menu_function_obj.deinitLVGL();
            wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
            //display_obj.exit_draw = true; // set everything back to normal
            menu_function_obj.orientDisplay();
            menu_function_obj.changeMenu(&menu_function_obj.apInfoMenu);
            wifi_scan_obj.RunAPInfo(i);
          }
        }
      }
      else {
        Serial.println("Exiting...");
        lv_obj_del_async(lv_obj_get_parent(lv_obj_get_parent(btn)));
  
        printf("LV_EVENT_CANCEL\n");
        menu_function_obj.deinitLVGL();
        wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
        display_obj.exit_draw = true; // set everything back to normal
      }
    }
  }

  void MenuFunctions::addSSIDGFX(){
    extern LinkedList<ssid>* ssids;
    
    String display_string = "";
    // Create a keyboard and apply the styles
    kb = lv_keyboard_create(lv_scr_act(), NULL);
    lv_obj_set_size(kb, LV_HOR_RES, LV_VER_RES / 2);
    lv_obj_set_event_cb(kb, add_ssid_keyboard_event_cb);
  
    // Create one text area
    // Store all SSIDs
    ta1 = lv_textarea_create(lv_scr_act(), NULL);
    lv_textarea_set_one_line(ta1, false);
    lv_obj_set_width(ta1, LV_HOR_RES);
    lv_obj_set_height(ta1, (LV_VER_RES / 2) - 35);
    lv_obj_set_pos(ta1, 5, 20);
    lv_textarea_set_cursor_hidden(ta1, true);
    lv_obj_align(ta1, NULL, LV_ALIGN_IN_TOP_MID, NULL, NULL);
    lv_textarea_set_placeholder_text(ta1, text_table1[0]);
  
    // Create second text area
    // Add SSIDs
    ta2 = lv_textarea_create(lv_scr_act(), ta1);
    lv_textarea_set_cursor_hidden(ta2, false);
    lv_textarea_set_one_line(ta2, true);
    lv_obj_align(ta2, NULL, LV_ALIGN_IN_TOP_MID, NULL, (LV_VER_RES / 2) - 35);
    lv_textarea_set_text(ta2, "");
    lv_textarea_set_placeholder_text(ta2, text_table1[1]);
    
  
    // After generating text areas, add text to first text box
    for (int i = 0; i < ssids->size(); i++)
      display_string.concat((String)ssids->get(i).essid + "\n");
      
    lv_textarea_set_text(ta1, display_string.c_str());
  
    // Focus it on one of the text areas to start
    lv_keyboard_set_textarea(kb, ta2);
    lv_keyboard_set_cursor_manage(kb, true);
    
  }
  
  // Keyboard callback dedicated to joining wifi
  void add_ssid_keyboard_event_cb(lv_obj_t * keyboard, lv_event_t event){
    extern Display display_obj;
    extern MenuFunctions menu_function_obj;
    extern WiFiScan wifi_scan_obj;
    extern LinkedList<ssid>* ssids;
    
    lv_keyboard_def_event_cb(kb, event);
  
    // User has applied text box
    if(event == LV_EVENT_APPLY){
      String display_string = "";
      printf("LV_EVENT_APPLY\n");
  
      // Get text from SSID text box
      String ta2_text = lv_textarea_get_text(ta2);
      Serial.println(ta2_text);
  
      // Add text box text to list of SSIDs
      wifi_scan_obj.addSSID(ta2_text);
  
      // Update large text box with ssid
      for (int i = 0; i < ssids->size(); i++)
        display_string.concat((String)ssids->get(i).essid + "\n");
      lv_textarea_set_text(ta1, display_string.c_str());
  
      lv_textarea_set_text(ta2, "");
    }else if(event == LV_EVENT_CANCEL){
      printf("LV_EVENT_CANCEL\n");
      menu_function_obj.deinitLVGL();
      display_obj.exit_draw = true; // set everything back to normal
    }
  }
  
  
  void ta_event_cb(lv_obj_t * ta, lv_event_t event)
  {
    if(event == LV_EVENT_CLICKED) {
      if(kb != NULL)
        lv_keyboard_set_textarea(kb, ta);
    }
  
  }

#endif
//// END LV_ARDUINO STUFF

void MenuFunctions::buttonNotSelected(int b, int x) {
  if (x == -1)
    x = b;

  // Ensure b is within valid button index range
  b = (x - menu_start_index) % BUTTON_SCREEN_LIMIT;

  uint16_t color = this->getColor(current_menu->list->get(x).color);

  #ifdef HAS_MINI_SCREEN
    display_obj.tft.setFreeFont(NULL);
    display_obj.key[b].drawButton(false, current_menu->list->get(x).name);
  #endif

  #ifdef HAS_FULL_SCREEN
    display_obj.tft.setFreeFont(MENU_FONT);
    display_obj.key[b].drawButton(false, current_menu->list->get(x).name);
    if ((current_menu->list->get(x).name != text09) && (current_menu->list->get(x).icon != 255))
          display_obj.tft.drawXBitmap(0,
                                      KEY_Y + (b * (KEY_H + KEY_SPACING_Y)) - (ICON_H / 2),
                                      menu_icons[current_menu->list->get(x).icon],
                                      ICON_W,
                                      ICON_H,
                                      dark_mode ? TFT_BLACK : TFT_WHITE,  // icon field follows the theme background
                                      color);
    this->redrawButtonBorder(b, false);
    display_obj.tft.setFreeFont(NULL);
  #endif
}

void MenuFunctions::buttonSelected(int b, int x) {
  if (x == -1)
    x = b;

  // Ensure b is within valid button index range
  b = (x - menu_start_index) % BUTTON_SCREEN_LIMIT;

  uint16_t color = this->getColor(current_menu->list->get(x).color);

  #ifdef HAS_MINI_SCREEN
    display_obj.tft.setFreeFont(NULL);
    display_obj.key[b].drawButton(true, current_menu->list->get(x).name);
  #endif

  #ifdef HAS_FULL_SCREEN
    display_obj.tft.setFreeFont(MENU_FONT);
    display_obj.key[b].drawButton(true, current_menu->list->get(x).name);
    if ((current_menu->list->get(x).name != text09) && (current_menu->list->get(x).icon != 255))
          display_obj.tft.drawXBitmap(0,
                                      KEY_Y + (b * (KEY_H + KEY_SPACING_Y)) - (ICON_H / 2),
                                      menu_icons[current_menu->list->get(x).icon],
                                      ICON_W,
                                      ICON_H,
                                      this->getColor(current_menu->list->get(x).color),  // pressed: field blends into inverted (color) fill
                                      dark_mode ? TFT_BLACK : TFT_WHITE);                 // glyph = pressed-text contrast, theme-aware
    this->redrawButtonBorder(b, true);
    display_obj.tft.setFreeFont(NULL);
  #endif
}

// Function to check menu input
void MenuFunctions::main(uint32_t currentTime)
{
    if (display_obj.exit_draw) {
        wifi_scan_obj.currentScanMode = WIFI_SCAN_OFF;
        display_obj.exit_draw = false;
        this->orientDisplay();
    }
    if ((wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF) ||
        (wifi_scan_obj.currentScanMode == OTA_UPDATE) ||
        (wifi_scan_obj.currentScanMode == ESP_UPDATE) ||
        (wifi_scan_obj.currentScanMode == SHOW_INFO) ||
        (wifi_scan_obj.currentScanMode == WIFI_SCAN_GPS_DATA) ||
        (wifi_scan_obj.currentScanMode == WIFI_SCAN_GPS_NMEA)) {
        if (wifi_scan_obj.orient_display) {
            this->orientDisplay();
            wifi_scan_obj.orient_display = false;
        }
    }

    if (currentTime != 0) {
      if (currentTime - initTime >= BANNER_TIME) {
        this->initTime = millis();
        if ((wifi_scan_obj.currentScanMode != LV_JOIN_WIFI) &&
            (wifi_scan_obj.currentScanMode != LV_ADD_SSID))
          this->updateStatusBar();

        // Do channel analyzer stuff
        if ((wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ANALYZER) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_ANALYZER)){
          #ifdef HAS_SCREEN
            this->setGraphScale(this->graphScaleCheck(wifi_scan_obj._analyzer_values));

            this->drawGraph(wifi_scan_obj._analyzer_values);
          #endif
        }
      }
    }

    static bool ignore_first_touch = true;

    
    boolean pressed = false;

    #if defined(CYD_32CAP) || defined(CYD_35CAP)
      int16_t t_x[5] = {0, 0, 0, 0, 0}, t_y[5] = {0, 0, 0, 0, 0}; // To store the touch coordinates
      int16_t points = 0;
    #else
      uint16_t t_x = 0, t_y = 0;
    #endif
    
    // Handle LVGL modes explicitly
    if (wifi_scan_obj.currentScanMode == LV_JOIN_WIFI || wifi_scan_obj.currentScanMode == LV_ADD_SSID) {
        display_obj.main(wifi_scan_obj.currentScanMode); // Calls lv_task_handler for LVGL
        return;
    }

    if ((wifi_scan_obj.currentScanMode != WIFI_SCAN_OFF) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_BEACON_SPAM) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_AP_SPAM) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_AUTH) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_DEAUTH) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_DEAUTH_MANUAL) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_DEAUTH_TARGETED) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_MIMIC) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_RICK_ROLL))
      display_obj.displayBuffer();


#ifdef CYD_24CAP
  int pre_getTouch = millis();
#endif

#ifdef CYD_24CAP || defined(CYD_22CAP)
  int pre_getTouchBBC = millis();
#endif

#if defined(HAS_ILI9341) || defined(HAS_ST7796) || defined(HAS_ST7789)
    #if defined(CYD_32CAP) || defined(CYD_35CAP)
      if (!this->disable_touch)
        points = this->updateTouch(t_x, t_y);
        pressed = points && points > 0;
    #else
      if (!this->disable_touch)
      pressed = this->updateTouch(&t_x, &t_y);
    #endif
      if (pressed) this->last_activity_ms = millis();   // reset the screensaver idle timer
      
    #ifdef HAS_SCREEN
    if ((wifi_scan_obj.currentScanMode != WIFI_SCAN_OFF) &&
        (pressed) &&
        (wifi_scan_obj.currentScanMode != OTA_UPDATE) &&
        (wifi_scan_obj.currentScanMode != ESP_UPDATE) &&
        (wifi_scan_obj.currentScanMode != SHOW_INFO) &&
        (wifi_scan_obj.currentScanMode != WIFI_SCAN_GPS_DATA) &&
        (wifi_scan_obj.currentScanMode != WIFI_SCAN_GPS_NMEA)) 
      {
        //Stop the current scan
        if ((wifi_scan_obj.currentScanMode == WIFI_SCAN_PROBE) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_STATION_WAR_DRIVE) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_STATION) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_AP) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_WAR_DRIVE) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_EVIL_PORTAL) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_TARGET_AP) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_TARGET_AP_FULL) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_AP_STA) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_PWN) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_ESPRESSIF) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_ALL) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_DEAUTH) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_BEACON_SPAM) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_AP_SPAM) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_AUTH) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_DEAUTH) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_DEAUTH_MANUAL) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_DEAUTH_TARGETED) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_MIMIC) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_RICK_ROLL) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_BEACON_LIST) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_ALL) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_AIRTAG) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_FLIPPER) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_SOUR_APPLE) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_SWIFTPAIR_SPAM) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_SPAM_ALL) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_SAMSUNG_SPAM) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_GOOGLE_SPAM) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_FLIPPER_SPAM) ||
            (wifi_scan_obj.currentScanMode == BT_SPOOF_AIRTAG) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_WAR_DRIVE) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_WAR_DRIVE_CONT) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_SKIMMERS) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_ANALYZER)) 
        {
            wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
            display_obj.tft.init();
            changeMenu(current_menu);
            delay(100);
        }

        x = -1;
        y = -1;

        return;
        
        /*for (int i = 0; i < BUTTON_ARRAY_LEN; i++) {
          display_obj.key[i].press(false);
        }*/
      }
  #endif

  #ifdef HAS_BUTTONS
    bool c_btn_press = c_btn.justPressed();

    #if defined(HAS_SCREEN)
      if ((c_btn_press) &&
          (wifi_scan_obj.currentScanMode != WIFI_SCAN_OFF) &&
          (wifi_scan_obj.currentScanMode != OTA_UPDATE) &&
          (wifi_scan_obj.currentScanMode != ESP_UPDATE) &&
          (wifi_scan_obj.currentScanMode != SHOW_INFO) &&
          (wifi_scan_obj.currentScanMode != WIFI_SCAN_GPS_DATA) &&
          (wifi_scan_obj.currentScanMode != WIFI_SCAN_GPS_NMEA))
        {
        if ((wifi_scan_obj.currentScanMode == WIFI_SCAN_PROBE) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_STATION_WAR_DRIVE) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_RAW_CAPTURE) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_STATION) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_AP) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_WAR_DRIVE) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_EVIL_PORTAL) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_SIG_STREN) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_TARGET_AP) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_TARGET_AP_FULL) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_AP_STA) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_PWN) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_ESPRESSIF) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_ALL) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_DEAUTH) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_BEACON_SPAM) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_AP_SPAM) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_AUTH) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_DEAUTH) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_DEAUTH_MANUAL) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_DEAUTH_TARGETED) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_MIMIC) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_RICK_ROLL) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_BEACON_LIST) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_ALL) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_SOUR_APPLE) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_SWIFTPAIR_SPAM) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_SPAM_ALL) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_SAMSUNG_SPAM) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_GOOGLE_SPAM) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_WAR_DRIVE) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_WAR_DRIVE_CONT) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_SKIMMERS) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_EAPOL) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_ACTIVE_EAPOL) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_ACTIVE_LIST_EAPOL) ||
            (wifi_scan_obj.currentScanMode == WIFI_PACKET_MONITOR) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ANALYZER) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_PACKET_RATE) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_ANALYZER))
          {
            wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
            display_obj.tft.init();
            changeMenu(current_menu);
            delay(100); // Brief delay to ignore residual touches after exit
          }

        x = -1;
        y = -1;

        /*for (int i = 0; i < BUTTON_ARRAY_LEN; i++) {
          display_obj.key[i].press(false);
        }*/
        return;
    }
  #endif
  
#endif

#if defined(HAS_ILI9341) || defined(HAS_ST7796) || defined(HAS_ST7789)
    if ((wifi_scan_obj.currentScanMode != WIFI_ATTACK_BEACON_SPAM) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_AP_SPAM) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_AUTH) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_DEAUTH) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_DEAUTH_MANUAL) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_DEAUTH_TARGETED) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_MIMIC) &&
        (wifi_scan_obj.currentScanMode != WIFI_SCAN_PACKET_RATE) &&
        (wifi_scan_obj.currentScanMode != WIFI_SCAN_RAW_CAPTURE) &&
        (wifi_scan_obj.currentScanMode != WIFI_SCAN_CHAN_ANALYZER) &&
        (wifi_scan_obj.currentScanMode != WIFI_SCAN_SIG_STREN) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_RICK_ROLL))
        {
          // --- Vertical touch-swipe scrolling (resistive CYD) --------------
          // A vertical drag moves the visible window; a swipe also suppresses the
          // tap it started on so you don't accidentally select an item mid-scroll.
          #if !defined(CYD_32CAP) && !defined(CYD_35CAP)
          {
            // Swipe to scroll: on RELEASE, jump by the number of rows dragged.
            // A live per-row scroll was tried but repainted the whole menu each
            // step and flickered badly on this panel; the single redraw here is
            // clean. True pixel-smooth (hardware) scrolling is a future task.
            static int s_swipe_start_y = -1;
            static int s_swipe_last_y = 0;
            const int SWIPE_THRESH = 40;  // px before a drag counts as a scroll
            this->menu_swipe_suppress = false;
            bool scrollable = current_menu && current_menu->list &&
                              ((int)current_menu->list->size() > BUTTON_SCREEN_LIMIT);
            if (pressed) {
              if (s_swipe_start_y < 0) { s_swipe_start_y = (int)t_y; s_swipe_last_y = (int)t_y; }
              else {
                s_swipe_last_y = (int)t_y;
                if (scrollable && abs((int)t_y - s_swipe_start_y) > SWIPE_THRESH)
                  this->menu_swipe_suppress = true;
              }
            } else {
              if (s_swipe_start_y >= 0) {
                int dy = s_swipe_last_y - s_swipe_start_y;
                if (scrollable && abs(dy) > SWIPE_THRESH) {
                  int rows = -dy / (KEY_H + KEY_SPACING_Y);   // swipe up -> later items
                  if (rows == 0) rows = (dy > 0) ? -1 : 1;
                  this->scrollMenu(rows);
                  this->menu_swipe_suppress = true;           // don't fire the tap
                }
              }
              s_swipe_start_y = -1;
            }
          }
          // ▲▼ scroll-arrow taps (right edge, generously padded hit zones for
          // resistive touch). Hold to repeat. Must come after the swipe block,
          // which clears menu_swipe_suppress each frame.
          // Scroll ONCE per tap, and suppress the button underneath for the whole
          // gesture (including the release frame) so an arrow tap can never also
          // select/back-out on the row it sits on.
          {
            static bool s_arrow_active = false;
            bool arrow_scrollable = current_menu && current_menu->list &&
                                    ((int)current_menu->list->size() > BUTTON_SCREEN_LIMIT);
            if (pressed && arrow_scrollable && ((int)t_x >= SCROLL_ARROW_X - 12)) {
              bool inUp = ((int)t_y >= SCROLL_UP_Y - 10 && (int)t_y <= SCROLL_UP_Y + SCROLL_ARROW_H + 10);
              bool inDn = ((int)t_y >= SCROLL_DN_Y - 10 && (int)t_y <= SCROLL_DN_Y + SCROLL_ARROW_H + 10);
              if (inUp || inDn) {
                if (!s_arrow_active) {           // leading edge only -> one row per tap
                  this->scrollMenu(inUp ? -1 : +1);
                  s_arrow_active = true;
                }
                this->menu_swipe_suppress = true;
              }
            }
            if (!pressed) {
              if (s_arrow_active) this->menu_swipe_suppress = true;  // eat the release tap
              s_arrow_active = false;
            }
          }
          #endif
          #if defined(CYD_32CAP) || defined(CYD_35CAP)
          for (uint8_t b = 0; b < BUTTON_ARRAY_LEN; b++) {
            bool found = false;
            if (pressed) {
                //Serial.print("Checking button "); Serial.print(b); Serial.print(" with "); Serial.print(points); Serial.println(" points");
                for (int16_t i = 0; i < points && i < 5; i++) {
                    if (display_obj.key[b].contains(t_x[i], t_y[i])) {
                        found = true;
                        //Serial.print("Button "); Serial.print(b); Serial.println(" pressed");
                        break;
                    }
                }
                display_obj.key[b].press(found);
            } else {
                display_obj.key[b].press(false);
            }
          }
          #else
          for (uint8_t b = 0; b < BUTTON_ARRAY_LEN; b++) {
            if (pressed && display_obj.key[b].contains(t_x, t_y)) {
                display_obj.key[b].press(true);
                //Serial.print("Button "); Serial.print(b); Serial.println(" pressed");
            } else {
                display_obj.key[b].press(false);
            }
          }
          #endif

          // Bug fix: the loop indexed the fixed-size key[] array by the menu's
          // item count, so any menu with more items than BUTTON_SCREEN_LIMIT
          // (e.g. WiFi Sniffers = 17, General = 16) ran past the array end and
          // crashed (LoadProhibited). Only the on-screen buttons are valid.
          uint8_t visible = min((int)current_menu->list->size(), BUTTON_SCREEN_LIMIT);
          // A swipe that scrolled the menu must not also fire the item it started on.
          for (uint8_t b = 0; b < visible && !this->menu_swipe_suppress; b++) {
            // Visible row b shows list item (b + menu_start_index) once scrolled.
            int idx = b + this->menu_start_index;
            display_obj.tft.setFreeFont(MENU_FONT);
            if (display_obj.key[b].justPressed()) {
                display_obj.key[b].drawButton(true, current_menu->list->get(idx).name); // Pressed state
                if (current_menu->list->get(idx).name != text09) {
                    uint16_t icon_color = this->itemColor(current_menu, idx);
                    display_obj.tft.setTextColor(icon_color, TFT_BLACK); // Set color state explicitly
                    display_obj.tft.drawXBitmap(0,
                                                KEY_Y + b * (KEY_H + KEY_SPACING_Y) - (ICON_H / 2),
                                                menu_icons[current_menu->list->get(idx).icon],
                                                ICON_W,
                                                ICON_H,
                                                this->itemColor(current_menu, idx),
                                                dark_mode ? TFT_BLACK : TFT_WHITE);  // glyph = pressed-text contrast, theme-aware
                }
                this->redrawButtonBorder(b, true);
            }

            // If button was just release, execute the button's function
        if ((display_obj.key[b].justReleased()) && (!pressed))
        {
          display_obj.key[b].drawButton(false, current_menu->list->get(idx).name);
          current_menu->list->get(idx).callable();
        }
        // This
        else if ((display_obj.key[b].justReleased()) && (pressed)) {
          display_obj.key[b].drawButton(false, current_menu->list->get(idx).name);
          if (current_menu->list->get(idx).name != text09)
            display_obj.tft.drawXBitmap(0,
                                        KEY_Y + b * (KEY_H + KEY_SPACING_Y) - (ICON_H / 2),
                                        menu_icons[current_menu->list->get(idx).icon],
                                        ICON_W,
                                        ICON_H,
                                        dark_mode ? TFT_BLACK : TFT_WHITE,  // icon field follows the theme background
                                        this->itemColor(current_menu, idx));
          this->redrawButtonBorder(b, false);
        }
  
        display_obj.tft.setFreeFont(NULL);
      }
    }
    x = -1;
    y = -1;

    /*for (int i = 0; i < BUTTON_ARRAY_LEN; i++) {
          display_obj.key[i].press(false);
        }*/
  #endif

  // Menu navigation and paging
  #ifdef HAS_BUTTONS
    #if !(defined(MARAUDER_V6) || defined(MARAUDER_V6_1))
    #ifndef MARAUDER_M5STICKC
    if (u_btn.justPressed()) {
        if (wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF || wifi_scan_obj.currentScanMode == OTA_UPDATE) {
            if (current_menu->selected > 0) {
                current_menu->selected--;
                if (current_menu->selected < this->menu_start_index) {
                    this->buildButtons(current_menu, current_menu->selected);
                    this->displayCurrentMenu(current_menu->selected);
                }
                this->buttonSelected(current_menu->selected - this->menu_start_index, current_menu->selected);
                if (!current_menu->list->get(current_menu->selected + 1).selected)
                    this->buttonNotSelected(current_menu->selected + 1 - this->menu_start_index, current_menu->selected + 1);
            } else {
                current_menu->selected = current_menu->list->size() - 1;
                if (current_menu->selected >= BUTTON_SCREEN_LIMIT) {
                    this->buildButtons(current_menu, current_menu->selected + 1 - BUTTON_SCREEN_LIMIT);
                    this->displayCurrentMenu(current_menu->selected + 1 - BUTTON_SCREEN_LIMIT);
                }
                this->buttonSelected(current_menu->selected, current_menu->selected);
                if (!current_menu->list->get(0).selected)
                    this->buttonNotSelected(0, this->menu_start_index);
              }
            }   
            else if ((wifi_scan_obj.currentScanMode == WIFI_PACKET_MONITOR) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_EAPOL) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ANALYZER) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_PACKET_RATE) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_RAW_CAPTURE) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_SIG_STREN)) {
            if (wifi_scan_obj.set_channel < 14)
              wifi_scan_obj.changeChannel(wifi_scan_obj.set_channel + 1);
            else
              wifi_scan_obj.changeChannel(1);
          }
        }
    #endif
    if (d_btn.justPressed()) {
        if (wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF || wifi_scan_obj.currentScanMode == OTA_UPDATE) {
            if (current_menu->selected < current_menu->list->size() - 1) {
                current_menu->selected++;
                // Page down
                if (current_menu->selected - this->menu_start_index >= BUTTON_SCREEN_LIMIT) {
                    this->buildButtons(current_menu, current_menu->selected + 1 - BUTTON_SCREEN_LIMIT);
                    this->displayCurrentMenu(current_menu->selected + 1 - BUTTON_SCREEN_LIMIT);
                }
                else
                  this->buttonSelected(current_menu->selected - this->menu_start_index, current_menu->selected);
                if (!current_menu->list->get(current_menu->selected - 1).selected)
                  this->buttonNotSelected(current_menu->selected - 1 - this->menu_start_index, current_menu->selected - 1);
              } else {
                if (current_menu->selected >= BUTTON_SCREEN_LIMIT) {
                    current_menu->selected = 0;
                    this->buildButtons(current_menu);
                    this->displayCurrentMenu();
                    this->buttonSelected(current_menu->selected);
              } 
            else {
              current_menu->selected = 0;
              //this->buildButtons(current_menu);  // Ensure all buttons are refreshed
              //this->displayCurrentMenu();
              this->buttonSelected(current_menu->selected);
              if (!current_menu->list->get(current_menu->list->size() - 1).selected)
                this->buttonNotSelected(current_menu->list->size() - 1);
              //if (!current_menu->list->get(current_menu->list->size() - 1).selected)
              //  this->buttonNotSelected(BUTTON_SCREEN_LIMIT - 1, current_menu->list->size() - 1);
                }
            }
          } 
          else if ((wifi_scan_obj.currentScanMode == WIFI_PACKET_MONITOR) ||
                (wifi_scan_obj.currentScanMode == WIFI_SCAN_EAPOL) ||
                (wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ANALYZER) ||
                (wifi_scan_obj.currentScanMode == WIFI_SCAN_PACKET_RATE) ||
                (wifi_scan_obj.currentScanMode == WIFI_SCAN_SIG_STREN)) {
          if (wifi_scan_obj.set_channel > 1)
            wifi_scan_obj.changeChannel(wifi_scan_obj.set_channel - 1);
          else
            wifi_scan_obj.changeChannel(14);
        }
      }
      if(c_btn_press){
        current_menu->list->get(current_menu->selected).callable();
      }
    #endif
  #endif

  // Hacker theme: live rain behind the menu instead of the marquee scroller.
  if (ui_theme == THEME_HACKER && wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF
      && current_menu && current_menu->list)
    this->stepHackerRain();
  else
    this->updateMarquees();   // scroll any menu labels too wide for the row

  // Idle screensaver after ~25s of no touch (all themes: Hacker rain, Pride
  // raining-men, or plain cycling quotes on Light/Dark).
  if (wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF
      && current_menu && current_menu->list
      && (millis() - this->last_activity_ms > 25000))
    this->runScreensaver();
}


#if BATTERY_ANALOG_ON == 1
byte battery_analog_array[10];
byte battery_count = 0;
byte battery_analog_last = 101;
#define BATTERY_CHECK 50
uint16_t battery_analog = 0;
void MenuFunctions::battery(bool initial)
{
  if (BATTERY_ANALOG_ON) {
    uint8_t n = 0;
    byte battery_analog_sample[10];
    byte deviation;
    if (battery_count == BATTERY_CHECK - 5)  digitalWrite(BATTERY_PIN, HIGH);
    else if (battery_count == 5) digitalWrite(BATTERY_PIN, LOW);
    if (battery_count == 0) {
      battery_analog = 0;
      for (n = 9; n > 0; n--)battery_analog_array[n] = battery_analog_array[n - 1];
      for (n = 0; n < 10; n++) {
        battery_analog_sample[n] = map((analogRead(ANALOG_PIN) * 5), 2400, 4200, 0, 100);
        if (battery_analog_sample[n] > 100) battery_analog_sample[n] = 100;
        else if (battery_analog_sample[n] < 0) battery_analog_sample[n] = 0;
        battery_analog += battery_analog_sample[n];
      }
      battery_analog = battery_analog / 10;
      for (n = 0; n < 10; n++) {
        deviation = abs(battery_analog - battery_analog_sample[n]);
        if (deviation >= 10) battery_analog_sample[n] = battery_analog;
      }
      battery_analog = 0;
      for (n = 0; n < 10; n++) battery_analog += battery_analog_sample[n];
      battery_analog = battery_analog / 10;
      battery_analog_array[0] = battery_analog;
      if (battery_analog_array[9] > 0 ) {
        battery_analog = 0;
        for (n = 0; n < 10; n++) battery_analog += battery_analog_array[n];
        battery_analog = battery_analog / 10;
      }
      battery_count ++;
    }
    else if (battery_count < BATTERY_CHECK) battery_count++;
    else if (battery_count >= BATTERY_CHECK) battery_count = 0;

    if (battery_analog_last != battery_analog) {
      battery_analog_last = battery_analog;
      MenuFunctions::battery2();
    }
  }
}
void MenuFunctions::battery2(bool initial)
{
  uint16_t the_color;
  Serial.println("battery2 called");
  if ( digitalRead(CHARGING_PIN) == 1) the_color = TFT_BLUE;
  else if (battery_analog < 20) the_color = TFT_RED;
  else if (battery_analog < 40)  the_color = TFT_YELLOW;
  else the_color = TFT_GREEN;

  display_obj.tft.setTextColor(the_color, STATUSBAR_COLOR);
  display_obj.tft.fillRect(186, 0, 50, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
  display_obj.tft.drawXBitmap(186,
                              0,
                              menu_icons[STATUS_BAT],
                              16,
                              16,
                              STATUSBAR_COLOR,
                              the_color);
  #ifdef HAS_ILI9341
    display_obj.tft.drawString((String)battery_analog + "%", 204, 0, 2);
  #endif

  #ifdef HAS_ST7796
    display_obj.tft.drawString((String)battery_analog + "%", 280, 0, 2);
  #endif

  #ifdef HAS_ST7789
    display_obj.tft.drawString((String)battery_analog + "%", 280, 0, 2);
  #endif
}
#else
void MenuFunctions::battery(bool initial)
{
  #ifdef HAS_BATTERY
    uint16_t the_color;
    if (battery_obj.i2c_supported)
    {
      
      // Could use int compare maybe idk
      if (((String)battery_obj.battery_level != "25") && ((String)battery_obj.battery_level != "0"))
        the_color = TFT_GREEN;
      else
        the_color = TFT_RED;

      if ((battery_obj.battery_level != battery_obj.old_level) || (initial)) {
        battery_obj.old_level = battery_obj.battery_level;
        display_obj.tft.fillRect(204, 0, SCREEN_WIDTH, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
      }

      display_obj.tft.setCursor(0, 1);
      /*if (!this->disable_touch) {
        display_obj.tft.drawXBitmap(186,
                                    0,
                                    menu_icons[STATUS_BAT],
                                    16,
                                    16,
                                    STATUSBAR_COLOR,
                                    the_color);
      }*/
      #ifdef HAS_ILI9341
        display_obj.tft.drawString((String)battery_obj.battery_level + "%", 204, 0, 2);
      #endif

      #ifdef HAS_ST7796
        display_obj.tft.drawString((String)battery_obj.battery_level + "%", 280, 0, 2);
      #endif

      #ifdef HAS_ST7789
        display_obj.tft.drawString((String)battery_obj.battery_level + "%", 280, 0, 2);
      #endif
    }
  #endif
}
void MenuFunctions::battery2(bool initial)
{
  MenuFunctions::battery(initial);
}
#endif

void MenuFunctions::updateStatusBar()
{
  display_obj.tft.setTextSize(1);

  bool status_changed = false;
  
  #if defined(MARAUDER_MINI) || defined(MARAUDER_M5STICKC) || defined(MARAUDER_REV_FEATHER)
    display_obj.tft.setFreeFont(NULL);
  #endif
  
  uint16_t the_color; 

  // GPS Stuff
  #ifdef HAS_GPS
    if (gps_obj.getGpsModuleStatus()) {
      if (gps_obj.getFixStatus())
        the_color = TFT_GREEN;
      else
        the_color = TFT_RED;
        
      #ifdef HAS_FULL_SCREEN
        uint8_t current_sat_count = gps_obj.getNumSats();
        
        if (current_sat_count != this->old_gps_sat_count) {
          
          display_obj.tft.fillRect(22, 0, 28, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
          this->old_gps_sat_count = current_sat_count;
        }
        
        display_obj.tft.drawXBitmap(4, 0, menu_icons[STATUS_GPS], 16, 16, STATUSBAR_COLOR, the_color);
        display_obj.tft.setTextColor(TFT_WHITE, STATUSBAR_COLOR);
        display_obj.tft.drawString(gps_obj.getNumSatsString(), 22, 0, 2);
      #elif defined(HAS_SCREEN)
        display_obj.tft.setTextColor(the_color, STATUSBAR_COLOR);
        display_obj.tft.drawString("GPS", 0, 0, 1);
      #endif
    }
  #endif

  display_obj.tft.setTextColor(TFT_WHITE, STATUSBAR_COLOR);

  // WiFi Channel Stuff
  if ((wifi_scan_obj.set_channel != wifi_scan_obj.old_channel) || (status_changed)) {
    wifi_scan_obj.old_channel = wifi_scan_obj.set_channel;
    #if defined(MARAUDER_MINI) || defined(MARAUDER_M5STICKC) || defined(MARAUDER_REV_FEATHER)
      display_obj.tft.fillRect(43, 0, TFT_WIDTH * 0.21, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
    #else
      display_obj.tft.fillRect(50, 0, TFT_WIDTH * 0.21, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
    #endif
    #ifdef HAS_ILI9341
      display_obj.tft.drawString("CH: " + (String)wifi_scan_obj.set_channel, 50, 0, 2);
    #endif

    #ifdef HAS_ST7796
      display_obj.tft.drawString("CH: " + (String)wifi_scan_obj.set_channel, 68, 0, 2);
    #endif

    #ifdef HAS_ST7789
      display_obj.tft.drawString("CH: " + (String)wifi_scan_obj.set_channel, 50, 0, 2);
    #endif

    #ifdef HAS_MINI_SCREEN
      display_obj.tft.drawString("CH: " + (String)wifi_scan_obj.set_channel, TFT_WIDTH/4, 0, 1);
    #endif
  }

  // RAM Stuff
  wifi_scan_obj.freeRAM();
  if ((wifi_scan_obj.free_ram != wifi_scan_obj.old_free_ram) || (status_changed)) {
    wifi_scan_obj.old_free_ram = wifi_scan_obj.free_ram;
    display_obj.tft.fillRect(100, 0, 60, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
    #ifdef HAS_ILI9341
      display_obj.tft.drawString((String)wifi_scan_obj.free_ram + "B", 100, 0, 2);
    #endif

    #ifdef HAS_ST7796
      display_obj.tft.drawString((String)wifi_scan_obj.free_ram + "B", 130, 0, 2);
    #endif

    #ifdef HAS_ST7789
      display_obj.tft.drawString((String)wifi_scan_obj.free_ram + "B", 100, 0, 2);
    #endif


    #ifdef HAS_MINI_SCREEN
      display_obj.tft.drawString((String)wifi_scan_obj.free_ram + "B", TFT_WIDTH/1.75, 0, 1);
    #endif
  }

  // Draw battery info
  #ifdef HAS_ILI9341
    MenuFunctions::battery(false);
    display_obj.tft.fillRect(186, 0, 16, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
  #endif

  #ifdef HAS_ST7796
    MenuFunctions::battery(false);
    display_obj.tft.fillRect(280, 0, 16, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
  #endif

  #ifdef HAS_ST7789
    MenuFunctions::battery(false);
    display_obj.tft.fillRect(186, 0, 16, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
  #endif

  
  #if defined(HAS_ILI9341) || defined(HAS_ST7796) || defined(HAS_ST7789)
    #ifdef HAS_BUTTONS
      if (this->disable_touch) {
        display_obj.tft.setCursor(0, 1);
        display_obj.tft.drawXBitmap(186,
                                    0,
                                    menu_icons[DISABLE_TOUCH],
                                    16,
                                    16,
                                    STATUSBAR_COLOR,
                                    TFT_RED);
      }
    #endif
  #endif


  // Draw SD info
  #ifdef HAS_SD
    if (sd_obj.supported)
      the_color = TFT_GREEN;
    else
      the_color = TFT_RED;

    #ifdef HAS_ILI9341
      display_obj.tft.drawXBitmap(170,
                                  0,
                                  menu_icons[STATUS_SD],
                                  16,
                                  16,
                                  STATUSBAR_COLOR,
                                  the_color);
    #endif

    #ifdef HAS_ST7796
      display_obj.tft.drawXBitmap(220,
                                  0,
                                  menu_icons[STATUS_SD],
                                  16,
                                  16,
                                  STATUSBAR_COLOR,
                                  the_color);
    #endif

    #ifdef HAS_ST7789
      display_obj.tft.drawXBitmap(170,
                                  0,
                                  menu_icons[STATUS_SD],
                                  16,
                                  16,
                                  STATUSBAR_COLOR,
                                  the_color);
    #endif
  #endif

  #ifdef HAS_MINI_SCREEN
    display_obj.tft.setTextColor(the_color, STATUSBAR_COLOR);
    display_obj.tft.drawString("SD", TFT_WIDTH - 12, 0, 1);
  #endif
}

void MenuFunctions::drawStatusBar()
{
  display_obj.tft.setTextSize(1);
  #ifdef HAS_MINI_SCREEN
    display_obj.tft.setFreeFont(NULL);
  #endif
  display_obj.tft.fillRect(0, 0, TFT_WIDTH, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
  display_obj.tft.setTextColor(TFT_WHITE, STATUSBAR_COLOR);

  uint16_t the_color;

  // GPS Stuff
  #ifdef HAS_GPS
    if (gps_obj.getGpsModuleStatus()) {
      if (gps_obj.getFixStatus())
        the_color = TFT_GREEN;
      else
        the_color = TFT_RED;
        
      #ifdef HAS_FULL_SCREEN
        display_obj.tft.drawXBitmap(4,
                                    0,
                                    menu_icons[STATUS_GPS],
                                    16,
                                    16,
                                    STATUSBAR_COLOR,
                                    the_color);
        display_obj.tft.setTextColor(TFT_WHITE, STATUSBAR_COLOR);

        display_obj.tft.drawString(gps_obj.getNumSatsString(), 22, 0, 2);
      #endif
    }
  #endif

  display_obj.tft.setTextColor(TFT_WHITE, STATUSBAR_COLOR);


  // WiFi Channel Stuff
  wifi_scan_obj.old_channel = wifi_scan_obj.set_channel;
  #ifdef HAS_MINI_SCREEN
    display_obj.tft.fillRect(43, 0, TFT_WIDTH * 0.21, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
  #else
    display_obj.tft.fillRect(50, 0, TFT_WIDTH * 0.21, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
  #endif
  #ifdef HAS_FULL_SCREEN
    #if defined(CYD_35CAP) || defined(CYD_35)
      display_obj.tft.drawString("CH: " + (String)wifi_scan_obj.set_channel, 68, 0, 2);
    #else
      display_obj.tft.drawString("CH: " + (String)wifi_scan_obj.set_channel, 50, 0, 2);
    #endif
  #endif

  #ifdef HAS_MINI_SCREEN
    display_obj.tft.drawString("CH: " + (String)wifi_scan_obj.set_channel, TFT_WIDTH/4, 0, 1);
  #endif

  // RAM Stuff
  wifi_scan_obj.freeRAM();
  wifi_scan_obj.old_free_ram = wifi_scan_obj.free_ram;
  display_obj.tft.fillRect(100, 0, 60, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
  #ifdef HAS_FULL_SCREEN
    #if defined(CYD_35CAP) || defined(CYD_35)
      display_obj.tft.drawString((String)wifi_scan_obj.free_ram + "B", 130, 0, 2);
    #else
      display_obj.tft.drawString((String)wifi_scan_obj.free_ram + "B", 100, 0, 2);
    #endif
  #endif

  #ifdef HAS_MINI_SCREEN
    display_obj.tft.drawString((String)wifi_scan_obj.free_ram + "B", TFT_WIDTH/1.75, 0, 1);
  #endif


  MenuFunctions::battery(true);
  display_obj.tft.fillRect(186, 0, 16, STATUS_BAR_WIDTH, STATUSBAR_COLOR);


  #if defined(HAS_ILI9341) || defined(HAS_ST7796) || defined(HAS_ST7789)
    #ifdef HAS_BUTTONS
      if (this->disable_touch) {
        display_obj.tft.setCursor(0, 1);
        display_obj.tft.drawXBitmap(186,
                                    0,
                                    menu_icons[DISABLE_TOUCH],
                                    16,
                                    16,
                                    STATUSBAR_COLOR,
                                    TFT_RED);
      }
    #endif
  #endif

  // Draw SD info
  #ifdef HAS_SD
    if (sd_obj.supported)
      the_color = TFT_GREEN;
    else
      the_color = TFT_RED;
  

    #ifdef HAS_ILI9341
      display_obj.tft.drawXBitmap(170,
                                  0,
                                  menu_icons[STATUS_SD],
                                  16,
                                  16,
                                  STATUSBAR_COLOR,
                                  the_color);
    #endif

    #ifdef HAS_ST7796
      display_obj.tft.drawXBitmap(220,
                                  0,
                                  menu_icons[STATUS_SD],
                                  16,
                                  16,
                                  STATUSBAR_COLOR,
                                  the_color);
    #endif

    #ifdef HAS_ST7789
      display_obj.tft.drawXBitmap(170,
                                  0,
                                  menu_icons[STATUS_SD],
                                  16,
                                  16,
                                  STATUSBAR_COLOR,
                                  the_color);
    #endif
  #endif

  #ifdef HAS_MINI_SCREEN
    display_obj.tft.setTextColor(the_color, STATUSBAR_COLOR);
    display_obj.tft.drawString("SD", TFT_WIDTH - 12, 0, 1);
  #endif
}

void MenuFunctions::orientDisplay()
{
  display_obj.tft.init();

  display_obj.tft.setRotation(0); // Portrait

  display_obj.tft.setCursor(0, 0);

  #ifdef HAS_SCREEN
    #ifdef CYD_28
      uint16_t calData[5] = { 350, 3465, 188, 3431, 2 }; // tft.setRotation(0); // Portrait with TFT Shield
    #elif defined(CYD_24)
      uint16_t calData[5] = { 481, 3053, 433, 3296, 3 }; // tft.setRotation(0); // Portrait with TFT Shield
    #elif defined(CYD_24CAP)
      uint16_t calData[5] = { 405, 3209, 297, 3314, 2 };
    #elif defined(CYD_24G)
      uint16_t calData[5] = { 405, 3209, 297, 3314, 2 }; // tft.setRotation(0); // Portrait with TFT Shield
    #elif defined(CYD_32)
      uint16_t calData[5] = { 251, 3539, 331, 3534, 6 }; // tft.setRotation(0); // Portrait with TFT Shield
    #elif defined(CYD_35)
      uint16_t calData[5] = { 309, 3465, 297, 3552, 6 };
    #elif defined(TFT_DIY)
      uint16_t calData[5] = { 339, 3470, 237, 3438, 2 }; // tft.setRotation(0); // Portrait with DIY TFT
    #endif

    #if !defined(CYD_32CAP) && !defined(CYD_35CAP)
      display_obj.tft.setTouch(calData);
    #endif
  #endif

  changeMenu(current_menu);
}

void MenuFunctions::runBoolSetting(String key) {
  display_obj.tftDrawRedOnOffButton();
}

String MenuFunctions::callSetting(String key) {
  specSettingMenu.name = key;
  
  String setting_type = settings_obj.getSettingType(key);

  if (setting_type == "bool") {
    return "bool";
  }
}

void MenuFunctions::displaySetting(String key, Menu* menu, int index) {
  specSettingMenu.name = key;

  bool setting_value = settings_obj.loadSetting<bool>(key);

  // Make a local copy of menu node
  MenuNode node = menu->list->get(index);

  display_obj.tft.setTextWrap(false);
  display_obj.tft.setFreeFont(NULL);
  display_obj.tft.setCursor(0, 100);
  display_obj.tft.setTextSize(1);

  // Set local copy value
  if (!setting_value) {
    display_obj.tft.setTextColor(TFT_RED);
    display_obj.tft.println(F(text_table1[4]));
    node.selected = false;
  }
  else {
    display_obj.tft.setTextColor(TFT_GREEN);
    display_obj.tft.println(F(text_table1[5]));
    node.selected = true;
  }

  // Put local copy back into menu
  menu->list->set(index, node);
    
}

// Function to build the menus
void MenuFunctions::RunSetup()
{
  extern LinkedList<AccessPoint>* access_points;
  extern LinkedList<Station>* stations;
  extern LinkedList<AirTag>* airtags;

  this->disable_touch = false;
  
  #if defined(HAS_ILI9341) || defined(HAS_ST7796) || defined(HAS_ST7789)
    this->initLVGL();
  #endif
   
  // root menu stuff
  mainMenu.list = new LinkedList<MenuNode>(); // Get list in first menu ready

  // Main menu stuff
  wifiMenu.list = new LinkedList<MenuNode>(); // Get list in second menu ready
  bluetoothMenu.list = new LinkedList<MenuNode>(); // Get list in third menu ready
  deviceMenu.list = new LinkedList<MenuNode>();
  #ifdef HAS_GPS
    if (gps_obj.getGpsModuleStatus()) {
      gpsInfoMenu.list = new LinkedList<MenuNode>();
    }
  #endif

  // Device menu stuff
  failedUpdateMenu.list = new LinkedList<MenuNode>();
  whichUpdateMenu.list = new LinkedList<MenuNode>();
  confirmMenu.list = new LinkedList<MenuNode>();
  updateMenu.list = new LinkedList<MenuNode>();
  settingsMenu.list = new LinkedList<MenuNode>();
  themeMenu.list = new LinkedList<MenuNode>();
  specSettingMenu.list = new LinkedList<MenuNode>();
  infoMenu.list = new LinkedList<MenuNode>();
  languageMenu.list = new LinkedList<MenuNode>();

  // WiFi menu stuff
  wifiSnifferMenu.list = new LinkedList<MenuNode>();
  wifiAttackMenu.list = new LinkedList<MenuNode>();
  #ifdef HAS_GPS
    wardrivingMenu.list = new LinkedList<MenuNode>();
  #endif
  wifiGeneralMenu.list = new LinkedList<MenuNode>();
  wifiAPMenu.list = new LinkedList<MenuNode>();
  apInfoMenu.list = new LinkedList<MenuNode>();
  setMacMenu.list = new LinkedList<MenuNode>();
  genAPMacMenu.list = new LinkedList<MenuNode>();
  #ifdef HAS_BT
    airtagMenu.list = new LinkedList<MenuNode>();
  #endif
  #if !defined(HAS_ILI9341) && !defined(HAS_ST7796) && !defined(HAS_ST7789)
    wifiStationMenu.list = new LinkedList<MenuNode>();
  #endif

  // WiFi HTML menu stuff
  htmlMenu.list = new LinkedList<MenuNode>();
  #if (!defined(HAS_ILI9341) && !defined(HAS_ST7796) && !defined(HAS_ST7789) && defined(HAS_BUTTONS))
    miniKbMenu.list = new LinkedList<MenuNode>();
  #endif
  #if !defined(HAS_ILI9341) && !defined(HAS_ST7796) && !defined(HAS_ST7789)
    #ifdef HAS_BUTTONS
      #ifdef HAS_SD
        sdDeleteMenu.list = new LinkedList<MenuNode>();
      #endif
    #endif
  #endif

  // Bluetooth menu stuff
  bluetoothSnifferMenu.list = new LinkedList<MenuNode>();
  bluetoothAttackMenu.list = new LinkedList<MenuNode>();

  // Settings stuff
  generateSSIDsMenu.list = new LinkedList<MenuNode>();
  clearSSIDsMenu.list = new LinkedList<MenuNode>();
  clearAPsMenu.list = new LinkedList<MenuNode>();
  saveFileMenu.list = new LinkedList<MenuNode>();

  saveSSIDsMenu.list = new LinkedList<MenuNode>();
  loadSSIDsMenu.list = new LinkedList<MenuNode>();
  saveAPsMenu.list = new LinkedList<MenuNode>();
  loadAPsMenu.list = new LinkedList<MenuNode>();
  saveATsMenu.list = new LinkedList<MenuNode>();
  loadATsMenu.list = new LinkedList<MenuNode>();

  // Work menu names
  mainMenu.name = text_table1[6];
  wifiMenu.name = text_table1[7];
  deviceMenu.name = text_table1[9];
  failedUpdateMenu.name = text_table1[11];
  whichUpdateMenu.name = text_table1[12];
  confirmMenu.name = text_table1[13];
  updateMenu.name = text_table1[15];
  languageMenu.name = text_table1[16]; 
  infoMenu.name = text_table1[17];
  settingsMenu.name = text_table1[18];
  themeMenu.name = "Toggle Theme";
  bluetoothMenu.name = text_table1[19];
  wifiSnifferMenu.name = text_table1[20];
  wifiAttackMenu.name = text_table1[21];
  wifiGeneralMenu.name = text_table1[22];
  saveFileMenu.name = "Save/Load Files";
  saveSSIDsMenu.name = "Save SSIDs";
  loadSSIDsMenu.name = "Load SSIDs";
  saveAPsMenu.name = "Save APs";
  loadAPsMenu.name = "Load APs";
  saveATsMenu.name = "Save Airtags";
  loadATsMenu.name = "Load Airtags";

  bluetoothSnifferMenu.name = text_table1[23];
  bluetoothAttackMenu.name = "Bluetooth Attacks";
  generateSSIDsMenu.name = text_table1[27];
  clearSSIDsMenu.name = text_table1[28];
  clearAPsMenu.name = text_table1[29];
  wifiAPMenu.name = "Access Points";
  apInfoMenu.name = "AP Info";
  setMacMenu.name = "Set MACs";
  genAPMacMenu.name = "Generate AP MAC";
  #ifdef HAS_BT
    airtagMenu.name = "Select Airtag";
  #endif
  #if !defined(HAS_ILI9341) && !defined(HAS_ST7796) && !defined(HAS_ST7789)
    wifiStationMenu.name = "Select Stations";
  #endif
  #ifdef HAS_GPS
    gpsInfoMenu.name = "GPS Data";
    wardrivingMenu.name = "Wardriving";
  #endif  
  htmlMenu.name = "EP HTML List";
  #if (!defined(HAS_ILI9341) && !defined(HAS_ST7796) && !defined(HAS_ST7789) && defined(HAS_BUTTONS))
    miniKbMenu.name = "Mini Keyboard";
  #endif
  #ifdef HAS_SD
    #if !defined(HAS_ILI9341) && !defined(HAS_ST7796) && !defined(HAS_ST7789) && !defined(HAS_ST7789)
      sdDeleteMenu.name = "Delete SD Files";
    #endif
  #endif

  // Build Main Menu
  mainMenu.parentMenu = NULL;
  this->addNodes(&mainMenu, text_table1[7], TFTGREEN, NULL, WIFI, [this]() {
    this->changeMenu(&wifiMenu);
  });
  this->addNodes(&mainMenu, text_table1[19], TFTCYAN, NULL, BLUETOOTH, [this]() {
    this->changeMenu(&bluetoothMenu);
  });
  this->addNodes(&mainMenu, text_table1[9], TFTBLUE, NULL, DEVICE, [this]() {
    this->changeMenu(&deviceMenu);
  });
  // Dual-boot hand-off: point the bootloader at the TouchBoard app (ota_1) and
  // reboot. TouchBoard's "Exit to Marauder" jumps back to ota_0. Both apps stay
  // resident in flash; nothing is copied.
  this->addNodes(&mainMenu, "TouchBoard", TFTORANGE, NULL, DEVICE, []() {
    const esp_partition_t* p = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    if (p) esp_ota_set_boot_partition(p);
    ESP.restart();
  });
  this->addNodes(&mainMenu, text_table1[30], TFTLIGHTGREY, NULL, REBOOT, []() {
    ESP.restart();
  });

  // Build WiFi Menu
  wifiMenu.parentMenu = &mainMenu; // Main Menu is second menu parent
  this->addNodes(&wifiMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(wifiMenu.parentMenu);
  });
  this->addNodes(&wifiMenu, text_table1[31], TFTYELLOW, NULL, SNIFFERS, [this]() {
    this->changeMenu(&wifiSnifferMenu);
  });
  #ifdef HAS_GPS
  this->addNodes(&wifiMenu, "Wardriving", TFTGREEN, NULL, BEACON_SNIFF, [this]() {
    this->changeMenu(&wardrivingMenu);
  });
  #endif
  this->addNodes(&wifiMenu, text_table1[32], TFTRED, NULL, ATTACKS, [this]() {
    this->changeMenu(&wifiAttackMenu);
  });
  this->addNodes(&wifiMenu, text_table1[33], TFTPURPLE, NULL, GENERAL_APPS, [this]() {
    this->changeMenu(&wifiGeneralMenu);
  });

  // Build WiFi sniffer Menu
  wifiSnifferMenu.parentMenu = &wifiMenu; // Main Menu is second menu parent
  this->addNodes(&wifiSnifferMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(wifiSnifferMenu.parentMenu);
  });
  this->addNodes(&wifiSnifferMenu, text_table1[42], TFTCYAN, NULL, PROBE_SNIFF, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(WIFI_SCAN_PROBE, TFT_CYAN);
  });
  this->addNodes(&wifiSnifferMenu, text_table1[43], TFTMAGENTA, NULL, BEACON_SNIFF, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(WIFI_SCAN_AP, TFT_MAGENTA);
  });
  this->addNodes(&wifiSnifferMenu, text_table1[44], TFTRED, NULL, DEAUTH_SNIFF, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(WIFI_SCAN_DEAUTH, TFT_RED);
  });

  this->addNodes(&wifiSnifferMenu, "Packet Count", TFTORANGE, NULL, PACKET_MONITOR, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(WIFI_SCAN_PACKET_RATE, TFT_ORANGE);
    wifi_scan_obj.renderPacketRate();
  });
  
  #if defined(HAS_ILI9341) || defined(HAS_ST7796) || defined(HAS_ST7789)
    this->addNodes(&wifiSnifferMenu, text_table1[46], TFTVIOLET, NULL, EAPOL, [this]() {
      wifi_scan_obj.StartScan(WIFI_SCAN_EAPOL, TFT_VIOLET);
    });
    this->addNodes(&wifiSnifferMenu, text_table1[45], TFTBLUE, NULL, PACKET_MONITOR, [this]() {
      wifi_scan_obj.StartScan(WIFI_PACKET_MONITOR, TFT_BLUE);
    });
  #else // No touch
    this->addNodes(&wifiSnifferMenu, text_table1[46], TFTVIOLET, NULL, EAPOL, [this]() {
      display_obj.clearScreen();
      this->drawStatusBar();
      wifi_scan_obj.StartScan(WIFI_SCAN_EAPOL, TFT_VIOLET);
    });
    this->addNodes(&wifiSnifferMenu, text_table1[45], TFTBLUE, NULL, PACKET_MONITOR, [this]() {
      display_obj.clearScreen();
      this->drawStatusBar();
      wifi_scan_obj.StartScan(WIFI_PACKET_MONITOR, TFT_BLUE);
    });
    /*this->addNodes(&wifiSnifferMenu, "Packet Count", TFTORANGE, NULL, PACKET_MONITOR, [this]() {
      display_obj.clearScreen();
      this->drawStatusBar();
      wifi_scan_obj.StartScan(WIFI_SCAN_PACKET_RATE, TFT_ORANGE);
      wifi_scan_obj.renderPacketRate();
    });*/
  #endif
  this->addNodes(&wifiSnifferMenu, "Channel Analyzer", TFTCYAN, NULL, PACKET_MONITOR, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    this->renderGraphUI(WIFI_SCAN_CHAN_ANALYZER);
    wifi_scan_obj.StartScan(WIFI_SCAN_CHAN_ANALYZER, TFT_CYAN);
  });
  this->addNodes(&wifiSnifferMenu, text_table1[58], TFTWHITE, NULL, PACKET_MONITOR, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(WIFI_SCAN_RAW_CAPTURE, TFT_WHITE);
  });
  this->addNodes(&wifiSnifferMenu, text_table1[47], TFTRED, NULL, PWNAGOTCHI, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(WIFI_SCAN_PWN, TFT_RED);
  });
  #if !defined(HAS_ILI9341) && !defined(HAS_ST7789) && !defined(HAS_ST7796)
    this->addNodes(&wifiSnifferMenu, text_table1[49], TFTMAGENTA, NULL, BEACON_SNIFF, [this]() {
      display_obj.clearScreen();
      this->drawStatusBar();
      wifi_scan_obj.StartScan(WIFI_SCAN_TARGET_AP, TFT_MAGENTA);
    });
  #endif
  this->addNodes(&wifiSnifferMenu, "Scan All", TFTLIME, NULL, BEACON_SNIFF, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(WIFI_SCAN_AP_STA, 0x97e0);
  });
  #if !defined(HAS_ILI9341) && !defined(HAS_ST7789) && !defined(HAS_ST7796)
  this->addNodes(&wifiSnifferMenu, text_table1[59], TFTORANGE, NULL, PACKET_MONITOR, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(WIFI_SCAN_STATION, TFT_WHITE);
  });
  #endif
  this->addNodes(&wifiSnifferMenu, "Signal Monitor", TFTCYAN, NULL, PACKET_MONITOR, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(WIFI_SCAN_SIG_STREN, TFT_CYAN);
  });
  
  // Build Wardriving menu
  #ifdef HAS_GPS
  wardrivingMenu.parentMenu = &wifiMenu; // Main Menu is second menu parent
  this->addNodes(&wardrivingMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(wardrivingMenu.parentMenu);
  });

  if (gps_obj.getGpsModuleStatus()) {
    this->addNodes(&wardrivingMenu, "Wardrive", TFTGREEN, NULL, BEACON_SNIFF, [this]() {
      display_obj.clearScreen();
      this->drawStatusBar();
      wifi_scan_obj.StartScan(WIFI_SCAN_WAR_DRIVE, TFT_GREEN);
    });

    this->addNodes(&wardrivingMenu, "Station Wardrive", TFTORANGE, NULL, PROBE_SNIFF, [this]() {
      display_obj.clearScreen();
      this->drawStatusBar();
      wifi_scan_obj.StartScan(WIFI_SCAN_STATION_WAR_DRIVE, TFT_ORANGE);
    });
  }

  #endif
  
  // Build WiFi attack menu
  wifiAttackMenu.parentMenu = &wifiMenu; // Main Menu is second menu parent
  this->addNodes(&wifiAttackMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(wifiAttackMenu.parentMenu);
  });
  this->addNodes(&wifiAttackMenu, text_table1[50], TFTRED, NULL, BEACON_LIST, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(WIFI_ATTACK_BEACON_LIST, TFT_RED);
  });
  this->addNodes(&wifiAttackMenu, text_table1[51], TFTORANGE, NULL, BEACON_SPAM, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(WIFI_ATTACK_BEACON_SPAM, TFT_ORANGE);
  });
  this->addNodes(&wifiAttackMenu, text_table1[52], TFTYELLOW, NULL, RICK_ROLL, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(WIFI_ATTACK_RICK_ROLL, TFT_YELLOW);
  });
  this->addNodes(&wifiAttackMenu, text_table1[53], TFTRED, NULL, PROBE_SNIFF, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(WIFI_ATTACK_AUTH, TFT_RED);
  });
  this->addNodes(&wifiAttackMenu, "Evil Portal", TFTORANGE, NULL, BEACON_SNIFF, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(WIFI_SCAN_EVIL_PORTAL, TFT_ORANGE);
    wifi_scan_obj.setMac();
  });
  this->addNodes(&wifiAttackMenu, text_table1[54], TFTRED, NULL, DEAUTH_SNIFF, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(WIFI_ATTACK_DEAUTH, TFT_RED);
  });
  this->addNodes(&wifiAttackMenu, text_table1[57], TFTMAGENTA, NULL, BEACON_LIST, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(WIFI_ATTACK_AP_SPAM, TFT_MAGENTA);
  });
  this->addNodes(&wifiAttackMenu, text_table1[62], TFTRED, NULL, DEAUTH_SNIFF, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(WIFI_ATTACK_DEAUTH_TARGETED, TFT_ORANGE);
  });
  // AP Mimic: re-broadcasts the SSIDs of APs you selected from a prior "Scan APs".
  // (Handler already present in WiFiScan.cpp; was just missing a menu launcher.)
  this->addNodes(&wifiAttackMenu, "AP Mimic", TFTMAGENTA, NULL, BEACON_LIST, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(WIFI_ATTACK_MIMIC, TFT_MAGENTA);
  });

  // Build WiFi General menu
  wifiGeneralMenu.parentMenu = &wifiMenu;
  this->addNodes(&wifiGeneralMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(wifiGeneralMenu.parentMenu);
  });
  this->addNodes(&wifiGeneralMenu, text_table1[27], TFTSKYBLUE, NULL, GENERATE, [this]() {
    this->changeMenu(&generateSSIDsMenu);
    wifi_scan_obj.RunGenerateSSIDs();
  });
  #if defined(HAS_ILI9341) || defined(HAS_ST7796) && !defined(CYD_35CAP) || defined(HAS_ST7789) && !defined(CYD_32CAP)
    this->addNodes(&wifiGeneralMenu, text_table1[1], TFTNAVY, NULL, KEYBOARD_ICO, [this](){
      display_obj.clearScreen(); 
      wifi_scan_obj.StartScan(LV_ADD_SSID, TFT_YELLOW); 
      addSSIDGFX();
    });
  #endif
  #if (!defined(HAS_ILI9341) && !defined(HAS_ST7796) && defined(HAS_BUTTONS))
    this->addNodes(&wifiGeneralMenu, text_table1[1], TFTNAVY, NULL, KEYBOARD_ICO, [this](){
      this->changeMenu(&miniKbMenu);
      this->miniKeyboard(&miniKbMenu);
    });
  #endif
  this->addNodes(&wifiGeneralMenu, text_table1[28], TFTSILVER, NULL, CLEAR_ICO, [this]() {
    this->changeMenu(&clearSSIDsMenu);
    wifi_scan_obj.RunClearSSIDs();
  });
  this->addNodes(&wifiGeneralMenu, text_table1[29], TFTDARKGREY, NULL, CLEAR_ICO, [this]() {
    this->changeMenu(&clearAPsMenu);
    wifi_scan_obj.RunClearAPs();
  });
  this->addNodes(&wifiGeneralMenu, text_table1[60], TFTBLUE, NULL, CLEAR_ICO, [this]() {
    this->changeMenu(&clearAPsMenu);
    wifi_scan_obj.RunClearStations();
  });
  #if defined(HAS_ILI9341) || defined(HAS_ST7796) || defined(HAS_ST7789)
    // Select APs on OG
    this->addNodes(&wifiGeneralMenu, "Select APs", TFTNAVY, NULL, KEYBOARD_ICO, [this](){
      // Add the back button
      display_obj.clearScreen(); 
      wifi_scan_obj.currentScanMode = LV_ADD_SSID; 
      wifi_scan_obj.StartScan(LV_ADD_SSID, TFT_RED);  
      addAPGFX();
    });
    // Select Stations on OG
    this->addNodes(&wifiGeneralMenu, text_table1[61], TFTLIGHTGREY, NULL, KEYBOARD_ICO, [this](){
      display_obj.clearScreen(); 
      wifi_scan_obj.currentScanMode = LV_ADD_SSID; 
      wifi_scan_obj.StartScan(LV_ADD_SSID, TFT_RED);  
      addStationGFX();
    });
    // Select Evil Portal Files on OG
    this->addNodes(&wifiGeneralMenu, "Select EP HTML File", TFTCYAN, NULL, KEYBOARD_ICO, [this](){
      display_obj.clearScreen(); 
      wifi_scan_obj.currentScanMode = LV_ADD_SSID; 
      wifi_scan_obj.StartScan(LV_ADD_SSID, TFT_RED);  
      selectEPHTMLGFX();
    });
    apInfoMenu.parentMenu = &wifiGeneralMenu;
    this->addNodes(&apInfoMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
      this->changeMenu(apInfoMenu.parentMenu);
    });
  #else // Mini EP HTML select
    this->addNodes(&wifiGeneralMenu, "Select EP HTML File", TFTCYAN, NULL, KEYBOARD_ICO, [this](){
      // Add the back button
      htmlMenu.list->clear();
        this->addNodes(&htmlMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
        this->changeMenu(htmlMenu.parentMenu);
      });

      // Populate the menu with buttons
      for (int i = 0; i < evil_portal_obj.html_files->size(); i++) {
        // This is the menu node
        this->addNodes(&htmlMenu, evil_portal_obj.html_files->get(i), TFTCYAN, NULL, 255, [this, i](){
          evil_portal_obj.selected_html_index = i;
          evil_portal_obj.target_html_name = evil_portal_obj.html_files->get(evil_portal_obj.selected_html_index);
          Serial.println("Set Evil Portal HTML as " + evil_portal_obj.target_html_name);
          evil_portal_obj.using_serial_html = false;
          this->changeMenu(htmlMenu.parentMenu);
          return;
        });
      }
      this->changeMenu(&htmlMenu);
    });

    #if (!defined(HAS_ILI9341) && !defined(HAS_ST7796) && !defined(HAS_ST7789) && defined(HAS_BUTTONS))
      miniKbMenu.parentMenu = &wifiGeneralMenu;
      this->addNodes(&miniKbMenu, "a", TFTCYAN, NULL, 0, [this]() {
        this->changeMenu(miniKbMenu.parentMenu);
      });
    #endif

    htmlMenu.parentMenu = &wifiGeneralMenu;
    this->addNodes(&htmlMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
      this->changeMenu(htmlMenu.parentMenu);
    });

    // Select APs on Mini
    this->addNodes(&wifiGeneralMenu, text_table1[56], TFTNAVY, NULL, KEYBOARD_ICO, [this](){
      wifiAPMenu.list->clear();
        this->addNodes(&wifiAPMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
        this->changeMenu(wifiAPMenu.parentMenu);
      });

      // Populate the menu with buttons
      for (int i = 0; i < access_points->size(); i++) {
        this->addNodes(&wifiAPMenu, access_points->get(i).essid, TFTCYAN, NULL, 255, [this, i](){
        AccessPoint new_ap = access_points->get(i);
        new_ap.selected = !access_points->get(i).selected;

        // Change selection status of menu node
        MenuNode new_node = current_menu->list->get(i + 1);
        new_node.selected = !current_menu->list->get(i + 1).selected;
        current_menu->list->set(i + 1, new_node);

        access_points->set(i, new_ap);
        }, access_points->get(i).selected);
      }
      this->changeMenu(&wifiAPMenu);
    });

    this->addNodes(&wifiGeneralMenu, "View AP Info", TFTCYAN, NULL, KEYBOARD_ICO, [this](){
      // Add the back button
      wifiAPMenu.list->clear();
        this->addNodes(&wifiAPMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
        this->changeMenu(wifiAPMenu.parentMenu);
      });

      // Populate the menu with buttons
      for (int i = 0; i < access_points->size(); i++) {
        // This is the menu node
        this->addNodes(&wifiAPMenu, access_points->get(i).essid, TFTCYAN, NULL, 255, [this, i](){
          this->changeMenu(&apInfoMenu);
          wifi_scan_obj.RunAPInfo(i);
        });
      }
      this->changeMenu(&wifiAPMenu);
    });

    apInfoMenu.parentMenu = &wifiAPMenu;
    this->addNodes(&apInfoMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
      this->changeMenu(apInfoMenu.parentMenu);
    });
    
    wifiAPMenu.parentMenu = &wifiGeneralMenu;
    this->addNodes(&wifiAPMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
      this->changeMenu(wifiAPMenu.parentMenu);
    });


    // Select Stations on Mini v2
    this->addNodes(&wifiGeneralMenu, "Select Stations", TFTCYAN, NULL, KEYBOARD_ICO, [this](){
      wifiAPMenu.list->clear();
        this->addNodes(&wifiAPMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
        this->changeMenu(wifiAPMenu.parentMenu);
      });

      int menu_limit = access_points->size();

      /*if (access_points->size() <= BUTTON_ARRAY_LEN)
        menu_limit = access_points->size();
      else
        menu_limit = BUTTON_ARRAY_LEN;*/

      for (int i = 0; i < menu_limit; i++) {
        wifiStationMenu.list->clear();
        // This is the menu node
        this->addNodes(&wifiAPMenu, access_points->get(i).essid, TFTCYAN, NULL, 255, [this, i](){

          wifiStationMenu.list->clear();

          // Add back button to the APs
          this->addNodes(&wifiStationMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
            this->changeMenu(wifiStationMenu.parentMenu);
          });

          // Add the AP's stations to the specific AP menu
          for (int x = 0; x < access_points->get(i).stations->size(); x++) {
            int cur_ap_sta = access_points->get(i).stations->get(x);

            this->addNodes(&wifiStationMenu, macToString(stations->get(cur_ap_sta)), TFTCYAN, NULL, 255, [this, i, cur_ap_sta, x](){
            Station new_sta = stations->get(cur_ap_sta);
            new_sta.selected = !stations->get(cur_ap_sta).selected;

            // Change selection status of menu node
            MenuNode new_node = current_menu->list->get(x + 1);
            new_node.selected = !current_menu->list->get(x + 1).selected;
            current_menu->list->set(x + 1, new_node);

            // Change selection status of button key
            //if (new_sta.selected) {
            //  this->buttonSelected(i + 1);
            //} else {
            //  this->buttonNotSelected(i + 1);
            //}

            stations->set(cur_ap_sta, new_sta);
            }, stations->get(cur_ap_sta).selected);
          }

          // Final change menu to the menu of Stations
          this->changeMenu(&wifiStationMenu);
          
        }, false);
      }
      this->changeMenu(&wifiAPMenu);
    });

    wifiStationMenu.parentMenu = &wifiAPMenu;
    this->addNodes(&wifiStationMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
      this->changeMenu(wifiStationMenu.parentMenu);
    });
  #endif

  #if defined(HAS_ILI9341) || defined(HAS_ST7789) || defined(HAS_ST7796)
    this->addNodes(&wifiGeneralMenu, "View AP Info", TFTLIGHTGREY, NULL, 0, [this]() {
      display_obj.clearScreen();
      wifi_scan_obj.currentScanMode = LV_ADD_SSID;
      wifi_scan_obj.StartScan(LV_ADD_SSID, TFT_WHITE);
      addAPGFX("AP Info");
    });
  #endif

  this->addNodes(&wifiGeneralMenu, "Set MACs", TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(&setMacMenu);
  });


  // Menu for generating and setting MAC addrs for AP and STA
  setMacMenu.parentMenu = &wifiGeneralMenu;
  this->addNodes(&setMacMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(setMacMenu.parentMenu);
  });

  // Generate random MAC for AP
  this->addNodes(&setMacMenu, "Generate AP MAC", TFTLIME, NULL, 0, [this]() {
    this->changeMenu(&genAPMacMenu);
    wifi_scan_obj.RunGenerateRandomMac(true);
  });

  // Generate random MAC for AP
  this->addNodes(&setMacMenu, "Generate STA MAC", TFTCYAN, NULL, 0, [this]() {
    this->changeMenu(&genAPMacMenu);
    wifi_scan_obj.RunGenerateRandomMac(false);
  });

  // Clone AP MAC to ESP32 for button folks
  #if !defined(HAS_ILI9341) && !defined(HAS_ST7789) && !defined(HAS_ST7789)
    this->addNodes(&setMacMenu, "Clone AP MAC", TFTRED, NULL, CLEAR_ICO, [this](){
      // Add the back button
      wifiAPMenu.list->clear();
        this->addNodes(&wifiAPMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
        this->changeMenu(wifiAPMenu.parentMenu);
      });

      // Populate the menu with buttons
      for (int i = 0; i < access_points->size(); i++) {
        // This is the menu node
        this->addNodes(&wifiAPMenu, access_points->get(i).essid, TFTLIME, NULL, 255, [this, i](){
          this->changeMenu(&genAPMacMenu);
          wifi_scan_obj.RunSetMac(access_points->get(i).bssid, true);
        });
      }
      this->changeMenu(&wifiAPMenu);
    });

    this->addNodes(&setMacMenu, "Clone STA MAC", TFTMAGENTA, NULL, CLEAR_ICO, [this](){
      // Add the back button
      wifiAPMenu.list->clear();
        this->addNodes(&wifiAPMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
        this->changeMenu(wifiAPMenu.parentMenu);
      });

      // Populate the menu with buttons
      for (int i = 0; i < stations->size(); i++) {
        // This is the menu node
        this->addNodes(&wifiAPMenu, macToString(stations->get(i).mac), TFTMAGENTA, NULL, 255, [this, i](){
          this->changeMenu(&genAPMacMenu);
          wifi_scan_obj.RunSetMac(stations->get(i).mac, false);
        });
      }
      this->changeMenu(&wifiAPMenu);
    });
  #endif

  // Menu for generating and setting access point MAC (just goes bacK)
  genAPMacMenu.parentMenu = &wifiGeneralMenu;
  this->addNodes(&genAPMacMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(genAPMacMenu.parentMenu);
  });
  
  // Build generate ssids menu
  generateSSIDsMenu.parentMenu = &wifiGeneralMenu;
  this->addNodes(&generateSSIDsMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(generateSSIDsMenu.parentMenu);
  });

  // Build clear ssids menu
  clearSSIDsMenu.parentMenu = &wifiGeneralMenu;
  this->addNodes(&clearSSIDsMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(clearSSIDsMenu.parentMenu);
  });
  clearAPsMenu.parentMenu = &wifiGeneralMenu;
  this->addNodes(&clearAPsMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(clearAPsMenu.parentMenu);
  });

  // Build Bluetooth Menu
  bluetoothMenu.parentMenu = &mainMenu; // Second Menu is third menu parent
  this->addNodes(&bluetoothMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(bluetoothMenu.parentMenu);
  });
  this->addNodes(&bluetoothMenu, text_table1[31], TFTYELLOW, NULL, SNIFFERS, [this]() {
    this->changeMenu(&bluetoothSnifferMenu);
  });
  this->addNodes(&bluetoothMenu, "Bluetooth Attacks", TFTRED, NULL, ATTACKS, [this]() {
    this->changeMenu(&bluetoothAttackMenu);
  });

  // Build bluetooth sniffer Menu
  bluetoothSnifferMenu.parentMenu = &bluetoothMenu; // Second Menu is third menu parent
  this->addNodes(&bluetoothSnifferMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(bluetoothSnifferMenu.parentMenu);
  });
  this->addNodes(&bluetoothSnifferMenu, text_table1[34], TFTGREEN, NULL, BLUETOOTH_SNIFF, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(BT_SCAN_ALL, TFT_GREEN);
  });
  this->addNodes(&bluetoothSnifferMenu, "Flipper Sniff", TFTORANGE, NULL, FLIPPER, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(BT_SCAN_FLIPPER, TFT_ORANGE);
  });
  this->addNodes(&bluetoothSnifferMenu, "Airtag Sniff", TFTWHITE, NULL, BLUETOOTH_SNIFF, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(BT_SCAN_AIRTAG, TFT_WHITE);
  });
  #ifdef HAS_GPS
    if (gps_obj.getGpsModuleStatus()) {
      this->addNodes(&bluetoothSnifferMenu, "BT Wardrive", TFTCYAN, NULL, BLUETOOTH_SNIFF, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(BT_SCAN_WAR_DRIVE, TFT_GREEN);
      });
      this->addNodes(&bluetoothSnifferMenu, "BT Wardrive Continuous", TFTRED, NULL, REBOOT, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(BT_SCAN_WAR_DRIVE_CONT, TFT_GREEN);
      });
    }
  #endif
  this->addNodes(&bluetoothSnifferMenu, text_table1[35], TFTMAGENTA, NULL, CC_SKIMMERS, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(BT_SCAN_SKIMMERS, TFT_MAGENTA);
  });
  this->addNodes(&bluetoothSnifferMenu, "Bluetooth Analyzer", TFTCYAN, NULL, PACKET_MONITOR, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    this->renderGraphUI(BT_SCAN_ANALYZER);
    wifi_scan_obj.StartScan(BT_SCAN_ANALYZER, TFT_CYAN);
  });

  // Bluetooth Attack menu
  bluetoothAttackMenu.parentMenu = &bluetoothMenu; // Second Menu is third menu parent
  this->addNodes(&bluetoothAttackMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(bluetoothAttackMenu.parentMenu);
  });
  this->addNodes(&bluetoothAttackMenu, "Sour Apple", TFTGREEN, NULL, DEAUTH_SNIFF, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(BT_ATTACK_SOUR_APPLE, TFT_GREEN);
  });
  this->addNodes(&bluetoothAttackMenu, "Swiftpair Spam", TFTCYAN, NULL, KEYBOARD_ICO, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(BT_ATTACK_SWIFTPAIR_SPAM, TFT_CYAN);
  });
  this->addNodes(&bluetoothAttackMenu, "Samsung BLE Spam", TFTRED, NULL, GENERAL_APPS, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(BT_ATTACK_SAMSUNG_SPAM, TFT_RED);
  });
  this->addNodes(&bluetoothAttackMenu, "Google BLE Spam", TFTPURPLE, NULL, LANGUAGE, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(BT_ATTACK_GOOGLE_SPAM, TFT_PURPLE);
  });
  this->addNodes(&bluetoothAttackMenu, "Flipper BLE Spam", TFTORANGE, NULL, FLIPPER, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(BT_ATTACK_FLIPPER_SPAM, TFT_ORANGE);
  });
  this->addNodes(&bluetoothAttackMenu, "BLE Spam All", TFTMAGENTA, NULL, DEAUTH_SNIFF, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(BT_ATTACK_SPAM_ALL, TFT_MAGENTA);
  });

  #if defined(HAS_ILI9341) || defined(HAS_ST7796) || defined(HAS_ST7789)
    this->addNodes(&bluetoothAttackMenu, "Spoof Airtag", TFTWHITE, NULL, ATTACKS, [this](){
      display_obj.clearScreen();
      wifi_scan_obj.currentScanMode = LV_ADD_SSID;
      wifi_scan_obj.StartScan(LV_ADD_SSID, TFT_WHITE);
      addAPGFX("Airtag");
    });
  #endif

  #if !defined(HAS_ILI9341) && !defined(HAS_ST7796) && !defined(HAS_ST7789)
    #ifdef HAS_BT
    // Select Airtag on Mini
      this->addNodes(&bluetoothAttackMenu, "Spoof Airtag", TFTWHITE, NULL, ATTACKS, [this](){
          // Clear nodes and add back button
          airtagMenu.list->clear();
          this->addNodes(&airtagMenu, text09, TFT_LIGHTGREY, NULL, 0, [this]() {
          this->changeMenu(airtagMenu.parentMenu);
        });

        // Add buttons for all airtags
        // Find out how big our menu is going to be
        int menu_limit;
        if (airtags->size() <= BUTTON_ARRAY_LEN)
          menu_limit = airtags->size();
        else
          menu_limit = BUTTON_ARRAY_LEN;

        // Create the menu nodes for all of the list items
        for (int i = 0; i < menu_limit; i++) {
          this->addNodes(&airtagMenu, airtags->get(i).mac, TFTWHITE, NULL, BLUETOOTH, [this, i](){
            AirTag new_at = airtags->get(i);
            new_at.selected = true;

            airtags->set(i, new_at);

            // Set all other airtags to "Not Selected"
            for (int x = 0; x < airtags->size(); x++) {
              if (x != i) {
                AirTag new_atx = airtags->get(x);
                new_atx.selected = false;
                airtags->set(x, new_atx);
              }
            }

            // Start the spoof
            display_obj.clearScreen();
            this->drawStatusBar();
            wifi_scan_obj.StartScan(BT_SPOOF_AIRTAG, TFT_WHITE);

          });
        }
        this->changeMenu(&airtagMenu);
      });

      airtagMenu.parentMenu = &bluetoothAttackMenu;
      this->addNodes(&airtagMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
        this->changeMenu(airtagMenu.parentMenu);
      });
    #endif

  #endif

  // Device menu
  deviceMenu.parentMenu = &mainMenu;
  this->addNodes(&deviceMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(deviceMenu.parentMenu);
  });
  this->addNodes(&deviceMenu, text_table1[15], TFTORANGE, NULL, UPDATE, [this]() {
    wifi_scan_obj.currentScanMode = OTA_UPDATE;
    this->changeMenu(&whichUpdateMenu);
  });

  this->addNodes(&deviceMenu, "Save/Load Files", TFTCYAN, NULL, SD_UPDATE, [this]() {
    this->changeMenu(&saveFileMenu);
  });

  this->addNodes(&deviceMenu, text_table1[16], TFTGREEN, NULL, LANGUAGE, [this]() {

    wifi_scan_obj.currentScanMode = SHOW_INFO;
    this->changeMenu(&languageMenu);   
  });
  this->addNodes(&deviceMenu, text_table1[17], TFTWHITE, NULL, DEVICE_INFO, [this]() {
    wifi_scan_obj.currentScanMode = SHOW_INFO;
    this->changeMenu(&infoMenu);
    wifi_scan_obj.RunInfo();
  });
  this->addNodes(&deviceMenu, text08, TFTNAVY, NULL, KEYBOARD_ICO, [this]() {
    this->changeMenu(&settingsMenu);
  });

  #ifdef HAS_SD
    if (sd_obj.supported) {
      this->addNodes(&deviceMenu, "Delete SD Files", TFTCYAN, NULL, SD_UPDATE, [this]() {
        #if !defined(HAS_ILI9341) && !defined(HAS_ST7796) && !defined(HAS_ST7789)
          #ifdef HAS_BUTTONS
            this->changeMenu(&sdDeleteMenu);
            #if !(defined(MARAUDER_V6) || defined(MARAUDER_V6_1))

              bool deleting = true;

              display_obj.tft.setTextWrap(false);
              display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
              display_obj.tft.setTextColor(TFT_CYAN, TFT_BLACK);
              display_obj.tft.println("Loading...");

              while (deleting) {
                // Build list of files
                sd_obj.sd_files->clear();
                delete sd_obj.sd_files;

                sd_obj.sd_files = new LinkedList<String>();

                sd_obj.sd_files->add("Back");

                sd_obj.listDirToLinkedList(sd_obj.sd_files);

                int sd_file_index = 0;

                this->sdDeleteMenu.list->set(0, MenuNode{sd_obj.sd_files->get(sd_file_index), false, TFTCYAN, 0, NULL, true, NULL});
                this->buildButtons(&sdDeleteMenu);
                this->displayCurrentMenu();

                // Start button loop
                while(true) {
                  #ifndef MARAUDER_M5STICKC
                    if (u_btn.justPressed()) {
                      if (sd_file_index > 0)
                        sd_file_index--;
                      else
                        sd_file_index = sd_obj.sd_files->size() - 1;

                      this->sdDeleteMenu.list->set(0, MenuNode{sd_obj.sd_files->get(sd_file_index), false, TFTCYAN, 0, NULL, true, NULL});
                      this->buildButtons(&sdDeleteMenu);
                      this->displayCurrentMenu();
                    }
                  #endif
                  if (d_btn.justPressed()) {
                    if (sd_file_index < sd_obj.sd_files->size() - 1)
                      sd_file_index++;
                    else
                      sd_file_index = 0;

                    this->sdDeleteMenu.list->set(0, MenuNode{sd_obj.sd_files->get(sd_file_index), false, TFTCYAN, 0, NULL, true, NULL});
                    this->buildButtons(&sdDeleteMenu, 0, sd_obj.sd_files->get(sd_file_index));
                    this->displayCurrentMenu();
                  }
                  if (c_btn.justPressed()) {
                    if (sd_obj.sd_files->get(sd_file_index) != "Back") {
                      if (sd_obj.removeFile("/" + sd_obj.sd_files->get(sd_file_index)))
                        Serial.println("Successfully Removed File: /" + sd_obj.sd_files->get(sd_file_index));
                        display_obj.tft.setTextWrap(false);
                        display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
                        display_obj.tft.setTextColor(TFT_CYAN, TFT_BLACK);
                        display_obj.tft.println("Deleting /" + sd_obj.sd_files->get(sd_file_index) + "...");
                    }
                    else {
                      this->changeMenu(sdDeleteMenu.parentMenu);
                      deleting = false;
                    }
                    break;
                  }
                }
              }
            #endif
          #endif
        #endif
      });
    }
  #endif

  #ifdef HAS_SD
    #if !defined(HAS_ILI9341) && !defined(HAS_ST7796) && !defined(HAS_ST7789)
      #ifdef HAS_BUTTONS
        sdDeleteMenu.parentMenu = &deviceMenu;
        this->addNodes(&sdDeleteMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
          this->changeMenu(sdDeleteMenu.parentMenu);
        });
      #endif
    #endif
  #endif

  // Save Files Menu
  saveFileMenu.parentMenu = &deviceMenu;
  this->addNodes(&saveFileMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(saveFileMenu.parentMenu);
  });
  this->addNodes(&saveFileMenu, "Save SSIDs", TFTCYAN, NULL, SD_UPDATE, [this]() {
    this->changeMenu(&saveSSIDsMenu);
    wifi_scan_obj.RunSaveSSIDList(true);
  });
  this->addNodes(&saveFileMenu, "Load SSIDs", TFTSKYBLUE, NULL, SD_UPDATE, [this]() {
    this->changeMenu(&loadSSIDsMenu);
    wifi_scan_obj.RunLoadSSIDList();
  });
  this->addNodes(&saveFileMenu, "Save APs", TFTNAVY, NULL, SD_UPDATE, [this]() {
    this->changeMenu(&saveAPsMenu);
    wifi_scan_obj.RunSaveAPList();
  });
  this->addNodes(&saveFileMenu, "Load APs", TFTBLUE, NULL, SD_UPDATE, [this]() {
    this->changeMenu(&loadAPsMenu);
    wifi_scan_obj.RunLoadAPList();
  });
  this->addNodes(&saveFileMenu, "Save Airtags", TFTWHITE, NULL, SD_UPDATE, [this]() {
    this->changeMenu(&saveAPsMenu);
    wifi_scan_obj.RunSaveATList();
  });
  this->addNodes(&saveFileMenu, "Load Airtags", TFTWHITE, NULL, SD_UPDATE, [this]() {
    this->changeMenu(&loadAPsMenu);
    wifi_scan_obj.RunLoadATList();
  });

  saveSSIDsMenu.parentMenu = &saveFileMenu;
  this->addNodes(&saveSSIDsMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(saveSSIDsMenu.parentMenu);
  });

  loadSSIDsMenu.parentMenu = &saveFileMenu;
  this->addNodes(&loadSSIDsMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(loadSSIDsMenu.parentMenu);
  });

  saveAPsMenu.parentMenu = &saveFileMenu;
  this->addNodes(&saveAPsMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(saveAPsMenu.parentMenu);
  });

  loadAPsMenu.parentMenu = &saveFileMenu;
  this->addNodes(&loadAPsMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(loadAPsMenu.parentMenu);
  });

  saveATsMenu.parentMenu = &saveFileMenu;
  this->addNodes(&saveATsMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(saveATsMenu.parentMenu);
  });

  loadATsMenu.parentMenu = &saveFileMenu;
  this->addNodes(&loadATsMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(loadATsMenu.parentMenu);
  });

  // GPS Menu
  #ifdef HAS_GPS
    if (gps_obj.getGpsModuleStatus()) {
      this->addNodes(&deviceMenu, "GPS Data", TFTRED, NULL, GPS_MENU, [this]() {
        wifi_scan_obj.currentScanMode = WIFI_SCAN_GPS_DATA;
        this->changeMenu(&gpsInfoMenu);
        wifi_scan_obj.StartScan(WIFI_SCAN_GPS_DATA, TFT_CYAN);
      });

      this->addNodes(&deviceMenu, "NMEA Stream", TFTORANGE, NULL, GPS_MENU, [this]() {
        wifi_scan_obj.currentScanMode = WIFI_SCAN_GPS_NMEA;
        this->changeMenu(&gpsInfoMenu);
        wifi_scan_obj.StartScan(WIFI_SCAN_GPS_NMEA, TFT_ORANGE);
      });

      // GPS Info Menu
      gpsInfoMenu.parentMenu = &deviceMenu;
      this->addNodes(&gpsInfoMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
        wifi_scan_obj.currentScanMode = WIFI_SCAN_OFF;
        wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
        this->changeMenu(gpsInfoMenu.parentMenu);
      }); 
    }
  #endif

  // Settings menu
  // Device menu
  settingsMenu.parentMenu = &deviceMenu;
  this->addNodes(&settingsMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    changeMenu(settingsMenu.parentMenu);
  });
  for (int i = 0; i < settings_obj.getNumberSettings(); i++) {
    if (this->callSetting(settings_obj.setting_index_to_name(i)) == "bool")
      this->addNodes(&settingsMenu, settings_obj.setting_index_to_name(i), TFTLIGHTGREY, NULL, 0, [this, i]() {
      settings_obj.toggleSetting(settings_obj.setting_index_to_name(i));
      this->changeMenu(&specSettingMenu);
      this->displaySetting(settings_obj.setting_index_to_name(i), &settingsMenu, i + 1);
    }, settings_obj.loadSetting<bool>(settings_obj.setting_index_to_name(i)));
  }

  // 4" CYD display controls: theme library + PWM brightness (10% steps)
  this->addNodes(&settingsMenu, "Toggle Theme", TFTCYAN, NULL, 0, [this]() {
    this->changeMenu(&themeMenu);
  });
  this->addNodes(&settingsMenu, "Brightness +", TFTYELLOW, NULL, 0, [this]() {
    backlight_pct = (backlight_pct <= 90) ? backlight_pct + 10 : 100;
    applyBrightness();
    saveDisplayPrefs();
  });
  this->addNodes(&settingsMenu, "Brightness -", TFTYELLOW, NULL, 0, [this]() {
    backlight_pct = (backlight_pct >= 20) ? backlight_pct - 10 : 10;
    applyBrightness();
    saveDisplayPrefs();
  });
  this->addNodes(&settingsMenu, "Calibrate Touch", TFTORANGE, NULL, 0, [this]() {
    this->runTouchCalibration();
  });

  // Theme library (opened by "Toggle Theme"). setTheme() applies + persists and
  // repaints in place, so you see the new look on this very menu.
  themeMenu.parentMenu = &settingsMenu;
  this->addNodes(&themeMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(themeMenu.parentMenu);
  });
  this->addNodes(&themeMenu, "Light Theme",  TFTWHITE,   NULL, 0, [this]() { this->setTheme(THEME_LIGHT);  });
  this->addNodes(&themeMenu, "Dark Theme",   TFTCYAN,    NULL, 0, [this]() { this->setTheme(THEME_DARK);   });
  this->addNodes(&themeMenu, "Hacker Theme", TFTGREEN,   NULL, 0, [this]() { this->setTheme(THEME_HACKER); });
  this->addNodes(&themeMenu, "Pride Theme",  TFTMAGENTA, NULL, 0, [this]() { this->setTheme(THEME_PRIDE);  });

  // Specific setting menu
  specSettingMenu.parentMenu = &settingsMenu;
  addNodes(&specSettingMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    this->changeMenu(specSettingMenu.parentMenu);
  });
 
  // Select update
  whichUpdateMenu.parentMenu = &deviceMenu;
  this->addNodes(&whichUpdateMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    wifi_scan_obj.currentScanMode = WIFI_SCAN_OFF;
    this->changeMenu(whichUpdateMenu.parentMenu);
  });
  #ifdef HAS_SD
    if (sd_obj.supported) addNodes(&whichUpdateMenu, text_table1[40], TFTMAGENTA, NULL, SD_UPDATE, [this]() {
      wifi_scan_obj.currentScanMode = OTA_UPDATE;
      this->changeMenu(&confirmMenu);
    });

    // Confirm SD update menu
    confirmMenu.parentMenu = &whichUpdateMenu;
    this->addNodes(&confirmMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
      this->changeMenu(confirmMenu.parentMenu);
    });
    this->addNodes(&confirmMenu, text14, TFTORANGE, NULL, UPDATE, [this]() {
      wifi_scan_obj.currentScanMode = OTA_UPDATE;
      this->changeMenu(&failedUpdateMenu);
      sd_obj.runUpdate();
    });
  #endif

  // Web Update
  updateMenu.parentMenu = &deviceMenu;

  // Failed update menu
  failedUpdateMenu.parentMenu = &whichUpdateMenu;
  this->addNodes(&failedUpdateMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    wifi_scan_obj.currentScanMode = WIFI_SCAN_OFF;
    this->changeMenu(failedUpdateMenu.parentMenu);
  });

  // Device info menu
  infoMenu.parentMenu = &deviceMenu;
  this->addNodes(&infoMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
    wifi_scan_obj.currentScanMode = WIFI_SCAN_OFF;
    this->changeMenu(infoMenu.parentMenu);
  });
  //language info menu
  languageMenu.parentMenu = &deviceMenu;
    this->addNodes(&languageMenu, text09, TFTLIGHTGREY, NULL, 0, [this]() {
      wifi_scan_obj.currentScanMode = WIFI_SCAN_OFF;
      this->changeMenu(infoMenu.parentMenu);
    });
  // Boot straight into the rain when Hacker theme is the saved default.
  if (ui_theme == THEME_HACKER) this->matrixSplash(1600);
  // Set the current menu to the mainMenu
  this->changeMenu(&mainMenu);

  this->initTime = millis();
  this->last_activity_ms = millis();   // start the screensaver idle timer fresh
}

#if (!defined(HAS_ILI9341) && !defined(HAS_ST7796)  && !defined(HAS_ST7789) && defined(HAS_BUTTONS))
  void MenuFunctions::miniKeyboard(Menu * targetMenu) {
    // Prepare a char array and reset temp SSID string
    extern LinkedList<ssid>* ssids;

    bool pressed = true;

    wifi_scan_obj.current_mini_kb_ssid = "";

    if (c_btn.isHeld()) {
      while (!c_btn.justReleased())
        delay(1);
    }

    int str_len = wifi_scan_obj.alfa.length() + 1; 

    char char_array[str_len];

    wifi_scan_obj.alfa.toCharArray(char_array, str_len);

    // Button loop until hold center button
    #ifdef HAS_BUTTONS
      #if !(defined(MARAUDER_V6) || defined(MARAUDER_V6_1))
        while(true) {
          // Cycle char previous
          #ifdef HAS_L
            if (l_btn.justPressed()) {
              pressed = true;
              if (this->mini_kb_index > 0)
                this->mini_kb_index--;
              else
                this->mini_kb_index = str_len - 2;

              targetMenu->list->set(0, MenuNode{String(char_array[this->mini_kb_index]).c_str(), false, TFTCYAN, 0, NULL, true, NULL});
              this->buildButtons(targetMenu);
              while (!l_btn.justReleased())
                delay(1);
            }
          #endif

          // Cycle char next
          #ifdef HAS_R
            if (r_btn.justPressed()) {
              pressed = true;
              if (this->mini_kb_index < str_len - 2)
                this->mini_kb_index++;
              else
                this->mini_kb_index = 0;

              targetMenu->list->set(0, MenuNode{String(char_array[this->mini_kb_index]).c_str(), false, TFTCYAN, 0, NULL, true, NULL});
              this->buildButtons(targetMenu, 0, String(char_array[this->mini_kb_index]).c_str());
              while (!r_btn.justReleased())
                delay(1);
            }
          #endif

          //// 5-WAY SWITCH STUFF
          // Add character
          #if (defined(HAS_D) && defined(HAS_R))
            if (d_btn.justPressed()) {
              pressed = true;
              wifi_scan_obj.current_mini_kb_ssid.concat(String(char_array[this->mini_kb_index]).c_str());
              while (!d_btn.justReleased())
                delay(1);
            }
          #endif

          // Remove character
          #if (defined(HAS_U) && defined(HAS_L))
            if (u_btn.justPressed()) {
              pressed = true;
              wifi_scan_obj.current_mini_kb_ssid.remove(wifi_scan_obj.current_mini_kb_ssid.length() - 1);
              while (!u_btn.justReleased())
                delay(1);
            }
          #endif

          //// PARTIAL SWITCH STUFF
          // Advance char or add char
          #if (defined(HAS_D) && !defined(HAS_R))
            if (d_btn.justPressed()) {
              bool was_held = false;
              pressed = true;
              while(!d_btn.justReleased()) {
                d_btn.justPressed();

                // Add letter to string
                if (d_btn.isHeld()) {
                  wifi_scan_obj.current_mini_kb_ssid.concat(String(char_array[this->mini_kb_index]).c_str());
                  was_held = true;
                  break;
                }
              }
              if (!was_held) {
                if (this->mini_kb_index < str_len - 2)
                  this->mini_kb_index++;
                else
                  this->mini_kb_index = 0;

                targetMenu->list->set(0, MenuNode{String(char_array[this->mini_kb_index]).c_str(), false, TFTCYAN, 0, NULL, true, NULL});
                this->buildButtons(targetMenu, 0, String(char_array[this->mini_kb_index]).c_str());
              }
            }
          #endif

          // Prev char or remove char
          #if (defined(HAS_U) && !defined(HAS_L))
            if (u_btn.justPressed()) {
              bool was_held = false;
              pressed = true;
              while(!u_btn.justReleased()) {
                u_btn.justPressed();

                // Remove letter from string
                if (u_btn.isHeld()) {
                  wifi_scan_obj.current_mini_kb_ssid.remove(wifi_scan_obj.current_mini_kb_ssid.length() - 1);
                  was_held = true;
                  break;
                }
              }
              if (!was_held) {
                if (this->mini_kb_index > 0)
                  this->mini_kb_index--;
                else
                  this->mini_kb_index = str_len - 2;

                targetMenu->list->set(0, MenuNode{String(char_array[this->mini_kb_index]).c_str(), false, TFTCYAN, 0, NULL, true, NULL});
                this->buildButtons(targetMenu);
              }
            }
          #endif

          // Add SSID
          #ifdef HAS_C
            if (c_btn.justPressed()) {
              while (!c_btn.justReleased()) {
                c_btn.justPressed(); // Need to continue updating button hold status. My shitty library.

                // Exit
                if (c_btn.isHeld()) {
                  this->changeMenu(targetMenu->parentMenu);
                  return;
                }
                delay(1);
              }
              // If we have a string, add it to list of SSIDs
              if (wifi_scan_obj.current_mini_kb_ssid != "") {
                pressed = true;
                ssid s = {wifi_scan_obj.current_mini_kb_ssid, random(1, 12), {random(256), random(256), random(256), random(256), random(256), random(256)}, false};
                ssids->unshift(s);
                wifi_scan_obj.current_mini_kb_ssid = "";
              }
            }
          #endif

          // Display info on screen
          if (pressed) {
            this->displayCurrentMenu();
            display_obj.tft.setTextWrap(false);
            display_obj.tft.fillRect(0, SCREEN_HEIGHT / 3, SCREEN_WIDTH, STATUS_BAR_WIDTH, TFT_BLACK);
            display_obj.tft.fillRect(0, SCREEN_HEIGHT / 3 + TEXT_HEIGHT * 2, SCREEN_WIDTH, STATUS_BAR_WIDTH, TFT_BLACK);
            display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
            display_obj.tft.setTextColor(TFT_CYAN, TFT_BLACK);
            display_obj.tft.println(wifi_scan_obj.current_mini_kb_ssid + "\n");
            display_obj.tft.setTextColor(TFT_GREEN, TFT_BLACK);

            display_obj.tft.println(ssids->get(0).essid);

            display_obj.tft.setTextColor(TFT_ORANGE, TFT_BLACK);
            display_obj.tft.println("U/D - Rem/Add Char");
            display_obj.tft.println("L/R - Prev/Nxt Char");
            display_obj.tft.println("C - Save");
            display_obj.tft.println("C(Hold) - Exit");
            pressed = false;
          }
        }
      #endif
    #endif
  }
#endif

// Function to show all MenuNodes in a Menu
void MenuFunctions::showMenuList(Menu * menu, int layer)
{
  // Iterate through all of the menu nodes in the menu
  for (uint8_t i = 0; i < menu->list->size(); i++)
  {
    // Depending on layer, indent
    for (uint8_t x = 0; x < layer * 4; x++)
      Serial.print(" ");
    Serial.print("Node: ");
    Serial.println(menu->list->get(i).name);
  }
  Serial.println();
}


// Function to add MenuNodes to a menu
/*void MenuFunctions::addNodes(Menu * menu, String name, uint16_t color, Menu * child, int place, std::function<void()> callable, bool selected, String command)
{
  TFT_eSPI_Button new_button;
  menu->list->add(MenuNode{name, false, color, place, &new_button, selected, callable});
  //menu->list->add(MenuNode{name, false, color, place, selected, callable});
}*/

void MenuFunctions::addNodes(Menu * menu, String name, uint8_t color, Menu * child, int place, std::function<void()> callable, bool selected, String command)
{
  TFT_eSPI_Button new_button;
  menu->list->add(MenuNode{name, false, color, place, &new_button, selected, callable});
  //menu->list->add(MenuNode{name, false, color, place, selected, callable});
}

void MenuFunctions::setGraphScale(float scale) {
  this->_graph_scale = scale;
}

float MenuFunctions::calculateGraphScale(int16_t value) {
  if (value < GRAPH_VERT_LIM) {
    return 1.0;  // No scaling needed if the value is within the limit
  }

  // Calculate the multiplier proportionally
  return (0.5 * GRAPH_VERT_LIM) / value;
}

float MenuFunctions::graphScaleCheck(const int16_t array[TFT_WIDTH]) {
  int16_t maxValue = 0;

  // Iterate through the array to find the highest value
  for (int16_t i = 0; i < TFT_WIDTH; i++) {
    if (array[i] > maxValue) {
      maxValue = array[i];
    }
  }

  // If the highest value exceeds GRAPH_VERT_LIM, call calculateMultiplier
  if (maxValue > GRAPH_VERT_LIM) {
    return this->calculateGraphScale(maxValue);
  }

  // If the highest value does not exceed GRAPH_VERT_LIM, return 1.0
  return 1.0;
}

void MenuFunctions::drawMaxLine(int16_t value, uint16_t color) {
  display_obj.tft.drawLine(0, TFT_HEIGHT - (value * this->_graph_scale), TFT_WIDTH, TFT_HEIGHT - (value * this->_graph_scale), color);
  display_obj.tft.setCursor(0, TFT_HEIGHT - (value * this->_graph_scale));
  display_obj.tft.setTextColor(color, TFT_BLACK);
  display_obj.tft.setTextSize(1);
  display_obj.tft.println((String)value);
}

void MenuFunctions::drawGraph(int16_t *values) {
  int16_t maxValue = 0;
  int total = 0;
  for (int i = TFT_WIDTH - 1; i >= 0; i--) {
    if (values[i] >= 0) {
      total = total + values[i];
      if (values[i] > maxValue) {
        maxValue = values[i];
      }
      display_obj.tft.drawLine(i, TFT_HEIGHT, i, TFT_HEIGHT - GRAPH_VERT_LIM, TFT_BLACK);
      display_obj.tft.drawLine(i, TFT_HEIGHT, i, TFT_HEIGHT - (values[i] * this->_graph_scale), TFT_CYAN);
    }
    else {
      int16_t ch_val = values[i] * -1;
      display_obj.tft.drawLine(i, TFT_HEIGHT, i, TFT_HEIGHT - GRAPH_VERT_LIM, TFT_BLACK);
      display_obj.tft.drawLine(i, TFT_HEIGHT, i, TFT_HEIGHT - GRAPH_VERT_LIM, TFT_RED);
      display_obj.tft.setCursor(i, TFT_HEIGHT - GRAPH_VERT_LIM);
      display_obj.tft.setTextColor(TFT_BLACK, TFT_RED);
      display_obj.tft.setTextSize(1);
      display_obj.tft.println((String)ch_val);
    }


  }

  this->drawMaxLine(maxValue, TFT_GREEN); // Draw max
  this->drawMaxLine(total / TFT_WIDTH, TFT_ORANGE); // Draw average
}

void MenuFunctions::renderGraphUI(uint8_t scan_mode) {
  display_obj.tft.setTextColor(TFT_WHITE, TFT_BLACK);
  if (scan_mode == WIFI_SCAN_CHAN_ANALYZER)
    display_obj.tft.drawCentreString("Frames/" + (String)BANNER_TIME + "ms", TFT_WIDTH / 2, TFT_HEIGHT - GRAPH_VERT_LIM - (CHAR_WIDTH * 2), 1);
  else if (scan_mode == BT_SCAN_ANALYZER)
    display_obj.tft.drawCentreString("BLE Beacons/" + (String)BANNER_TIME + "ms", TFT_WIDTH / 2, TFT_HEIGHT - GRAPH_VERT_LIM - (CHAR_WIDTH * 2), 1);
  display_obj.tft.drawLine(0, TFT_HEIGHT - GRAPH_VERT_LIM - 1, TFT_WIDTH, TFT_HEIGHT - GRAPH_VERT_LIM - 1, TFT_WHITE);
  display_obj.tft.setCursor(0, TFT_HEIGHT - GRAPH_VERT_LIM - (CHAR_WIDTH * 8));
  display_obj.tft.setTextSize(1);
  display_obj.tft.setTextColor(TFT_GREEN, TFT_BLACK);
  display_obj.tft.println("Max");
  display_obj.tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  display_obj.tft.println("Average");
  display_obj.tft.setTextColor(TFT_RED, TFT_BLACK);
  if (scan_mode != BT_SCAN_ANALYZER)
    display_obj.tft.println("Channel Marker");
}

// HSV (h in degrees 0..360, s/v 0..1) -> RGB565. Used by the Pride rainbow.
static uint16_t hsv565(float h, float s, float v) {
  float c = v * s;
  float x = c * (1 - fabsf(fmodf(h / 60.0f, 2.0f) - 1));
  float m = v - c;
  float r, g, b;
  if      (h <  60) { r = c; g = x; b = 0; }
  else if (h < 120) { r = x; g = c; b = 0; }
  else if (h < 180) { r = 0; g = c; b = x; }
  else if (h < 240) { r = 0; g = x; b = c; }
  else if (h < 300) { r = x; g = 0; b = c; }
  else              { r = c; g = 0; b = x; }
  uint8_t R = (uint8_t)((r + m) * 255), G = (uint8_t)((g + m) * 255), B = (uint8_t)((b + m) * 255);
  return ((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3);
}

// Hacker theme text/icon palette: green-dominant with occasional neon blue/cyan
// and a rare yellow, cycled by menu rank so adjacent rows read differently.
// All greens are the same bright neon-green (darker greens were unreadable);
// cyan/blue/yellow stay as sparse, still-legible accents for row contrast.
static const uint16_t HACKER_PAL[] = {
  0x07E0,  // neon green
  0x07FF,  // neon cyan
  0x07E0,  // neon green
  0x055F,  // neon blue
  0x07E0,  // neon green
  0xFFE0,  // yellow (sparse)
};

// Themed colour for one menu item. Pride = rainbow spread red->violet with the
// Back row held grey; Hacker = cycled greens/blues; Dark/Light = the base map.
uint16_t MenuFunctions::itemColor(Menu* menu, int absIndex) {
  if (!menu || !menu->list || absIndex < 0 || absIndex >= menu->list->size())
    return getColor(TFTLIGHTGREY);
  MenuNode item = menu->list->get(absIndex);
  bool isBack = (item.name == text09);

  if (ui_theme == THEME_PRIDE) {
    if (isBack) return TFT_DARKGREY;                 // Back always grey, easy to find
    int total = 0, rank = 0;                         // rank among the non-Back rows
    for (int i = 0; i < menu->list->size(); i++) {
      if (menu->list->get(i).name == text09) continue;
      if (i == absIndex) rank = total;
      total++;
    }
    float h = (total <= 1) ? 0.0f : (280.0f * rank / (total - 1));  // first=red(0) .. last=violet(280)
    return hsv565(h, 1.0f, 1.0f);
  }

  if (ui_theme == THEME_HACKER) {
    if (isBack) return TFT_DARKGREY;
    int rank = 0;
    for (int i = 0; i < absIndex; i++)
      if (menu->list->get(i).name != text09) rank++;
    return HACKER_PAL[rank % (int)(sizeof(HACKER_PAL) / sizeof(HACKER_PAL[0]))];
  }

  return getColor(item.color);   // Dark / Light: existing behaviour
}

// Button-outline colour per theme (Hacker gets a terminal-style grey frame).
uint16_t MenuFunctions::themeOutline() {
  if (ui_theme == THEME_HACKER) return TFT_LIGHTGREY;
  return dark_mode ? TFT_WHITE : TFT_BLACK;
}

// Apply + persist a theme and repaint the current menu so the change is live.
void MenuFunctions::setTheme(uint8_t t) {
  ui_theme = t;
  dark_mode = (ui_theme != THEME_LIGHT);
  saveDisplayPrefs();
  if (ui_theme == THEME_HACKER) this->matrixSplash(1600);   // "entering the matrix"
  this->buildButtons(current_menu, this->menu_start_index);
  this->displayCurrentMenu(this->menu_start_index);
}

// Draw a small procedural glyph (22x22) in place of the normal menu icon, for
// the Hacker and Pride themes. rank cycles the set so adjacent rows differ.
void MenuFunctions::drawThemeGlyph(int x, int y, int rank, uint16_t color) {
  TFT_eSPI& t = display_obj.tft;
  int cx = x + ICON_W / 2, cy = y + ICON_H / 2;

  if (ui_theme == THEME_PRIDE) {
    switch (rank % 6) {
      case 0: {  // pride flag: six ROYGBIV stripes
        static const uint16_t rc[6] = { TFT_RED, TFT_ORANGE, TFT_YELLOW, TFT_GREEN, TFT_BLUE, TFT_PURPLE };
        int sh = ICON_H / 6;
        for (int s = 0; s < 6; s++) t.fillRect(x + 2, y + s * sh, ICON_W - 4, sh, rc[s]);
        break;
      }
      case 1:    // heart
        t.fillCircle(cx - 3, cy - 2, 3, TFT_RED);
        t.fillCircle(cx + 3, cy - 2, 3, TFT_RED);
        t.fillTriangle(cx - 6, cy - 1, cx + 6, cy - 1, cx, cy + 7, TFT_RED);
        break;
      case 2:    // peace sign
        t.drawCircle(cx, cy, 8, TFT_WHITE);
        t.drawFastVLine(cx, cy - 8, 16, TFT_WHITE);
        t.drawLine(cx, cy, cx - 6, cy + 6, TFT_WHITE);
        t.drawLine(cx, cy, cx + 6, cy + 6, TFT_WHITE);
        break;
      case 3: {  // transgender-pride stripes (light blue / pink / white)
        uint16_t ts[5] = { 0x5D1F, 0xFDB8, TFT_WHITE, 0xFDB8, 0x5D1F };
        int sh = ICON_H / 5;
        for (int s = 0; s < 5; s++) t.fillRect(x + 2, y + s * sh, ICON_W - 4, sh, ts[s]);
        break;
      }
      case 4:    // figure with a rounded belly
        t.fillCircle(cx - 2, y + 4, 3, color);
        t.drawFastVLine(cx - 2, y + 7, 7, color);
        t.fillCircle(cx + 2, cy + 3, 4, color);
        t.drawFastVLine(cx - 2, cy + 6, 4, color);
        break;
      default: {  // adult-novelty: vertical shaft + two circles at the base
        uint16_t pc = TFT_PURPLE;
        t.fillRoundRect(cx - 3, y + 1, 6, 15, 3, pc);   // shaft (rounded-top cylinder)
        t.fillCircle(cx - 4, y + 17, 3, pc);            // base circle, left
        t.fillCircle(cx + 4, y + 17, 3, pc);            // base circle, right
        break;
      }
    }
    return;
  }

  // Hacker glyphs: single-colour line art (the row's hacker colour) on black.
  switch (rank % 6) {
    case 0:  // terminal  >_
      t.drawLine(x + 3, cy - 5, x + 9, cy, color);
      t.drawLine(x + 9, cy, x + 3, cy + 5, color);
      t.fillRect(x + 11, cy + 4, 8, 2, color);
      break;
    case 1:  // skull
      t.fillCircle(cx, cy - 1, 7, color);
      t.fillRect(cx - 4, cy + 4, 8, 4, color);
      t.fillCircle(cx - 3, cy - 1, 2, TFT_BLACK);
      t.fillCircle(cx + 3, cy - 1, 2, TFT_BLACK);
      t.fillRect(cx - 1, cy + 4, 2, 4, TFT_BLACK);
      break;
    case 2:  // bug
      t.fillCircle(cx, cy + 1, 5, color);
      t.fillCircle(cx, cy - 5, 3, color);
      for (int s = -1; s <= 1; s++) t.drawFastHLine(cx - 10, cy + s * 3, 5, color);
      for (int s = -1; s <= 1; s++) t.drawFastHLine(cx + 5,  cy + s * 3, 5, color);
      break;
    case 3:  // padlock
      t.fillRoundRect(cx - 6, cy, 12, 9, 2, color);
      t.drawCircle(cx, cy - 1, 4, color);   // shackle (approx)
      break;
    case 4:  // signal waves
      t.fillCircle(cx - 5, cy + 5, 2, color);
      t.drawCircle(cx - 5, cy + 5, 6, color);
      t.drawCircle(cx - 5, cy + 5, 10, color);
      break;
    default: // chip
      t.drawRect(cx - 5, cy - 5, 10, 10, color);
      t.fillRect(cx - 2, cy - 2, 4, 4, color);
      for (int s = -3; s <= 3; s += 3) {
        t.drawFastVLine(cx + s, cy - 8, 3, color);
        t.drawFastVLine(cx + s, cy + 5, 3, color);
      }
      break;
  }
}

// Draw the left-edge icon for a menu item: normal XBitmap in Dark/Light, or a
// themed procedural glyph in Hacker/Pride. Back rows never get an icon.
void MenuFunctions::drawItemIcon(Menu* menu, int absIndex, int iconY, uint16_t color) {
  if (!menu || !menu->list || absIndex < 0 || absIndex >= menu->list->size()) return;
  MenuNode item = menu->list->get(absIndex);
  if (item.name == text09) return;
  if (ui_theme == THEME_HACKER || ui_theme == THEME_PRIDE) {
    int rank = 0;
    for (int i = 0; i < absIndex; i++)
      if (menu->list->get(i).name != text09) rank++;
    this->drawThemeGlyph(0, iconY, rank, color);
    return;
  }
  if (item.icon == 255) return;
  display_obj.tft.drawXBitmap(0, iconY, menu_icons[item.icon], ICON_W, ICON_H,
                              dark_mode ? TFT_BLACK : TFT_WHITE, color);
}

// Full-screen "Matrix" digital rain, played as a splash when Hacker theme is
// entered (and at boot in Hacker). Blocking for duration_ms — it only runs at
// theme-switch / boot, so it never touches menu snappiness. Each column has a
// bright green head, a medium-green trail, and a black tail that erases behind.
void MenuFunctions::matrixSplash(uint32_t duration_ms) {
  TFT_eSPI& t = display_obj.tft;
  int W = t.width(), H = t.height();
  const int CW = 8, CH = 10, TRAIL = 7;     // char cell + trail length (rows)
  int ncols = W / CW; if (ncols > 60) ncols = 60;
  int nrows = H / CH;
  static int drop[60];
  for (int c = 0; c < ncols; c++) drop[c] = -(int)random(0, nrows);   // stagger starts above the top

  const char* SET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ#$%&*+<>=/\\";
  int nch = strlen(SET);

  t.fillScreen(TFT_BLACK);
  t.setFreeFont(NULL);          // built-in 6x8 font
  t.setTextSize(1);
  t.setTextDatum(TL_DATUM);

  uint32_t start = millis();
  while (millis() - start < duration_ms) {
    for (int c = 0; c < ncols; c++) {
      int y = drop[c], x = c * CW;
      char buf[2] = {0, 0};
      if (y >= 0 && y < nrows) {                 // bright head
        buf[0] = SET[random(0, nch)];
        t.setTextColor(0x07E0, TFT_BLACK);
        t.drawString(buf, x, y * CH);
      }
      if (y - 1 >= 0 && y - 1 < nrows) {         // fade the previous head to mid-green
        buf[0] = SET[random(0, nch)];
        t.setTextColor(0x0320, TFT_BLACK);
        t.drawString(buf, x, (y - 1) * CH);
      }
      int yt = y - TRAIL;                         // erase the tail
      if (yt >= 0 && yt < nrows) t.fillRect(x, yt * CH, CW, CH, TFT_BLACK);
      drop[c]++;
      if (drop[c] - TRAIL > nrows) drop[c] = -(int)random(0, 24);   // recycle above the top
    }
    delay(45);                                   // ~22 fps
  }
  t.setTextDatum(TL_DATUM);
}

// One live "digital rain" frame drawn over the (all-black) Hacker menu, then
// the outlines/labels/glyphs re-stamped on top so they stay readable. Because
// the menu background AND button fills are black, this needs no alpha — the
// rain simply falls behind the floating chrome. Throttled to keep touch snappy.
void MenuFunctions::stepHackerRain() {
  uint32_t now = millis();
  if (now - hrain_tick < 60) return;   // ~16 fps
  hrain_tick = now;

  TFT_eSPI& t = display_obj.tft;
  int W = t.width(), H = t.height();
  int top = KEY_Y - KEY_H / 2;         // rain lives below the banner/status bar
  if (top < 0) top = 0;
  const int CW = 8, CH = 10, TRAIL = 6;
  int ncols = W / CW; if (ncols > 64) ncols = 64;
  int nrows = (H - top) / CH;
  static const char* SET = "0123456789ABCDEF#$%&*+<>=/";
  int nch = strlen(SET);

  // Protected strips: the glyph+label of each visible row (x from 0 to just past
  // the label). Rain skips any cell overlapping these, so the text is never
  // overwritten — no flicker — and the drops flow above/below/right of it.
  int nvis = min(current_menu->list->size() - this->menu_start_index, (int)BUTTON_SCREEN_LIMIT);
  int bx1[BUTTON_SCREEN_LIMIT], by0[BUTTON_SCREEN_LIMIT], by1[BUTTON_SCREEN_LIMIT];
  t.setFreeFont(MENU_FONT);
  for (int b = 0; b < nvis; b++) {
    int idx = b + this->menu_start_index;
    int cy  = KEY_Y + b * (KEY_H + KEY_SPACING_Y);
    bx1[b] = BUTTON_PADDING + t.textWidth(current_menu->list->get(idx).name) + 2;
    by0[b] = cy - 12;
    by1[b] = cy + 13;
  }

  t.setFreeFont(NULL);
  t.setTextSize(1);
  t.setTextDatum(TL_DATUM);
  for (int c = 0; c < ncols; c++) {
    int y = hrain_drop[c], x = c * CW;
    char buf[2] = {0, 0};
    for (int k = 0; k < 2; k++) {                 // k=0 bright head, k=1 dim trail
      int yy = y - k;
      if (yy < 0 || yy >= nrows) continue;
      int py = top + yy * CH;
      bool blocked = false;
      for (int b = 0; b < nvis && !blocked; b++)
        if (x < bx1[b] && py < by1[b] && py + CH > by0[b]) blocked = true;
      if (blocked) continue;                       // leave the label/glyph untouched
      buf[0] = SET[random(0, nch)];
      t.setTextColor(k == 0 ? 0x07E0 : 0x0320, TFT_BLACK);
      t.drawString(buf, x, py);
    }
    int yt = y - TRAIL;
    if (yt >= 0 && yt < nrows) {
      int py = top + yt * CH;
      bool blocked = false;
      for (int b = 0; b < nvis && !blocked; b++)
        if (x < bx1[b] && py < by1[b] && py + CH > by0[b]) blocked = true;
      if (!blocked) t.fillRect(x, py, CW, CH, TFT_BLACK);
    }
    hrain_drop[c]++;
    if (hrain_drop[c] - TRAIL > nrows) hrain_drop[c] = -(int)random(0, 20);
  }
  this->restampHackerMenu();
}

// Redraw the visible buttons' outline + label + glyph on top of the rain, with
// NO fill so the rain shows through the button interiors. Labels use an opaque
// black box so the text stays crisp while rain animates around it.
void MenuFunctions::restampHackerMenu() {
  if (!current_menu || !current_menu->list) return;
  TFT_eSPI& t = display_obj.tft;
  int visible = min(current_menu->list->size() - this->menu_start_index, (int)BUTTON_SCREEN_LIMIT);
  int r = min((int)KEY_W, (int)KEY_H) / 4;
  // Only the outlines (and arrows) are redrawn each frame — the rain crosses
  // those. Labels/glyphs are drawn ONCE by displayCurrentMenu and the rain skips
  // their cells, so we must NOT repaint them here (repainting every frame was
  // the shimmer the user saw).
  for (int b = 0; b < visible; b++) {
    int cy = KEY_Y + b * (KEY_H + KEY_SPACING_Y);
    t.drawRoundRect(0, cy - KEY_H / 2, KEY_W, KEY_H, r, this->themeOutline());
  }
  this->drawScrollArrows();   // keep the ▲▼ crisp over the rain
}

// Dimmed (~20%) idle screensaver. Hacker = full-screen green rain; Pride = a
// rainbow backdrop with little "man" figures raining down. Blocks until touch,
// then restores brightness and repaints the menu. Entered only for those two
// themes after a stretch of no touch.
void MenuFunctions::runScreensaver() {
  TFT_eSPI& t = display_obj.tft;
  int W = t.width(), H = t.height();
  bool pride = (ui_theme == THEME_PRIDE);

  uint8_t saved_bl = backlight_pct;
  backlight_pct = 40;                 // dim while the screensaver runs
  applyBrightness();

  if (pride)                          // paint the rainbow backdrop once
    for (int y = 0; y < H; y++) t.drawFastHLine(0, y, W, hsv565(300.0f * y / H, 0.9f, 0.45f));
  else
    t.fillScreen(TFT_BLACK);

  bool hacker = (ui_theme == THEME_HACKER);

  // Cycling headline (protected band in the vertical centre, redrawn only on
  // change so the animation never flickers it). Theme-specific phrases first,
  // then the universal quotes (shown on every theme).
  static const char* PRIDE_TXT[] = { "Slay Queen", "Yasss", "Love is Love", "Born this Way", "It's raining men" };
  static const char* HACK_TXT[]  = { "Enter the Matrix", "Zero-Day Exploit", "I'm bypassing the firewall",
                                     "Backlooping through the Mainframe", "Come on, baby, talk to me",
                                     "Now, we wait", "Too Easy" };
  static const char* COMMON_TXT[] = {
    "Differential girdlespring: Connects the up-and-down parts to the analytical line.",
    "Sperry bearings: Parts used to balance the machine against a specific type of fake magnetic pull.",
    "Panendermic semi-boloid slots: Grooves cut into the base to help hold the fake gears.",
    "Non-reversible tremie pipe: A specialized tube used to control the fake flow of liquid.",
    "Lotus-o-delta type stator: A main power coil that helps stop electric feedback.",
    "Capacitive diractance: A fake electronic force that works against regular power resistance.",
    "Malleable logarithmic casing: The strong outer shell designed to hold the gears together." };
  const char** BASE = pride ? PRIDE_TXT : (hacker ? HACK_TXT : NULL);
  int nbase = pride ? 5 : (hacker ? 7 : 0);
  int ncommon = 7;
  int NPHR = pride ? nbase : (nbase + ncommon);   // Pride shows only its own phrases (no technobabble)
  int phrase = -1;
  uint32_t lastPhrase = 0;
  bool firstPhrase = true;
  int bcy = H / 2;                                            // headline centre
  int bandY0 = H / 2, bandY1 = H / 2;                         // current protected band (grows per phrase)
  int prevBandY0 = H / 2, prevBandY1 = H / 2;                 // previous band, cleared on change

  const int CW = 8, CH = 10, TRAIL = 7;       // hacker rain cells
  const int MW = 22, MSTEP = 9;               // pride man column width + fall speed (px)
  int ncols = (pride ? W / MW : W / CW); if (ncols > 64) ncols = 64;
  int nrows = H / CH;
  for (int c = 0; c < ncols; c++) hrain_drop[c] = pride ? -(int)random(0, H) : -(int)random(0, nrows);

  static const char* SET = "0123456789ABCDEF#$%&*+<>=/";
  int nch = strlen(SET);

  uint16_t tx = 0, ty = 0;
  while (!this->updateTouch(&tx, &ty)) {       // run until touched
    if (pride) {
      for (int c = 0; c < ncols; c++) {
        int x = c * MW, y = hrain_drop[c];
        for (int yy = y; yy < y + 22 && yy < H; yy++)     // erase old man; leave the headline band alone
          if (yy >= 0 && (yy < bandY0 || yy >= bandY1)) t.drawFastHLine(x, yy, MW, hsv565(300.0f * yy / H, 0.9f, 0.45f));
        hrain_drop[c] += MSTEP;
        if (hrain_drop[c] > H) hrain_drop[c] = -(int)random(0, 80) - 20;
        int ny = hrain_drop[c];
        if (ny >= -20 && ny < H && (ny + 22 <= bandY0 || ny >= bandY1)) {   // hide men behind the headline
          int mcx = x + MW / 2;
          t.fillCircle(mcx, ny + 4, 3, TFT_WHITE);
          t.drawFastVLine(mcx, ny + 7, 8, TFT_WHITE);
          t.drawFastHLine(mcx - 5, ny + 9, 11, TFT_WHITE);
          t.drawLine(mcx, ny + 15, mcx - 5, ny + 20, TFT_WHITE);
          t.drawLine(mcx, ny + 15, mcx + 5, ny + 20, TFT_WHITE);
        }
      }
    } else if (hacker) {
      t.setFreeFont(NULL); t.setTextSize(1); t.setTextDatum(TL_DATUM);
      for (int c = 0; c < ncols; c++) {
        int y = hrain_drop[c], x = c * CW;
        char buf[2] = {0, 0};
        int hy = y * CH, dy = (y - 1) * CH, ty2 = (y - TRAIL) * CH;
        bool hb = (hy + CH > bandY0 && hy < bandY1);       // in the headline band?
        bool db = (dy + CH > bandY0 && dy < bandY1);
        bool tb = (ty2 + CH > bandY0 && ty2 < bandY1);
        if (y >= 0 && y < nrows && !hb) { buf[0] = SET[random(0, nch)]; t.setTextColor(0x07E0, TFT_BLACK); t.drawString(buf, x, hy); }
        if (y - 1 >= 0 && y - 1 < nrows && !db) { buf[0] = SET[random(0, nch)]; t.setTextColor(0x0320, TFT_BLACK); t.drawString(buf, x, dy); }
        int yt = y - TRAIL;
        if (yt >= 0 && yt < nrows && !tb) t.fillRect(x, ty2, CW, CH, TFT_BLACK);
        hrain_drop[c]++;
        if (hrain_drop[c] - TRAIL > nrows) hrain_drop[c] = -(int)random(0, 20);
      }
    }

    // Headline cycling (~8s): theme phrases then the universal quotes, big
    // (~24pt) monospace, word-wrapped, in a band the animation avoids.
    uint32_t now = millis();
    if (firstPhrase || now - lastPhrase > 8000) {
      firstPhrase = false; lastPhrase = now;
      int np = 0;                                    // random order, avoiding an immediate repeat
      if (NPHR > 1) { do { np = random(0, NPHR); } while (np == phrase); }
      phrase = np;
      const char* s = (phrase < nbase) ? BASE[phrase] : COMMON_TXT[phrase - nbase];

      // Build wrapped lines (each records its font size). The "Term: definition"
      // quotes get a 24pt term (x3) + 18pt definition (x2); everything else is a
      // single 24pt block. All lines are word-wrapped and centre-aligned.
      char lineText[12][40]; int lineSize[12]; int nAll = 0;
      auto wrapInto = [&](const char* str, int sz) {
        int cw = 6 * sz, cap = (W - 8) / cw; if (cap < 1) cap = 1;
        char work[224]; strncpy(work, str, sizeof(work) - 1); work[sizeof(work) - 1] = 0;
        char cur[40]; cur[0] = 0; int curlen = 0;
        for (char* tok = strtok(work, " "); tok && nAll < 12; tok = strtok(NULL, " ")) {
          int tl = strlen(tok);
          if (curlen == 0) { strncpy(cur, tok, 39); cur[39] = 0; curlen = tl; }
          else if (curlen + 1 + tl <= cap) { strcat(cur, " "); strncat(cur, tok, 39 - curlen - 1); curlen += 1 + tl; }
          else { strncpy(lineText[nAll], cur, 39); lineText[nAll][39] = 0; lineSize[nAll] = sz; nAll++; strncpy(cur, tok, 39); cur[39] = 0; curlen = tl; }
        }
        if (curlen > 0 && nAll < 12) { strncpy(lineText[nAll], cur, 39); lineText[nAll][39] = 0; lineSize[nAll] = sz; nAll++; }
      };
      const char* colon = strchr(s, ':');
      if (colon) {
        char term[80]; int tn = (int)(colon - s) + 1; if (tn > 79) tn = 79;
        strncpy(term, s, tn); term[tn] = 0;                       // "Term:" (includes the colon)
        const char* def = colon + 1; while (*def == ' ') def++;    // skip the space after ':'
        wrapInto(term, 3);                                        // 24pt
        wrapInto(def, 2);                                         // 18pt
      } else {
        wrapInto(s, 3);
      }

      // Clear the previous band, compute the new one centred, remember it.
      if (pride) for (int y = prevBandY0; y < prevBandY1; y++) t.drawFastHLine(0, y, W, hsv565(300.0f * y / H, 0.9f, 0.45f));
      else       t.fillRect(0, prevBandY0, W, prevBandY1 - prevBandY0, TFT_BLACK);
      int totalH = 0; for (int i = 0; i < nAll; i++) totalH += 8 * lineSize[i] + 4;
      int y0 = bcy - totalH / 2;
      bandY0 = y0 - 2; bandY1 = y0 + totalH + 2;
      prevBandY0 = bandY0; prevBandY1 = bandY1;

      t.setFreeFont(NULL); t.setTextDatum(TL_DATUM);
      int gi = 0, total = strlen(s), ly = y0;
      for (int i = 0; i < nAll; i++) {
        int sz = lineSize[i], cw = 6 * sz, lh = 8 * sz + 4, llen = strlen(lineText[i]);
        int lx = (W - llen * cw) / 2; if (lx < 0) lx = 0;         // centre-align each line
        t.setTextSize(sz);
        if (pride) {                                              // per-char rainbow, transparent over backdrop
          for (int j = 0; j < llen; j++) {
            char cbuf[2] = { lineText[i][j], 0 };
            t.setTextColor(hsv565((float)gi / (total > 1 ? total - 1 : 1) * 300.0f, 1.0f, 1.0f));
            t.drawString(cbuf, lx + j * cw, ly);
            gi++;
          }
          gi++;
        } else {                                                  // Hacker = green, others = white, on black
          t.setTextColor(hacker ? 0x07E0 : TFT_WHITE, TFT_BLACK);
          t.drawString(lineText[i], lx, ly);
        }
        ly += lh;
      }
    }
    delay(50);                                  // ~20 fps
  }

  while (this->updateTouch(&tx, &ty)) delay(10);   // wait for release so the wake tap doesn't select
  this->last_activity_ms = millis();
  backlight_pct = saved_bl;
  applyBrightness();
  this->buildButtons(current_menu, this->menu_start_index);
  this->displayCurrentMenu(this->menu_start_index);
}

uint16_t MenuFunctions::getColor(uint16_t color) {
  // Base palette
  uint16_t c;
  if (color == TFTWHITE) c = TFT_WHITE;
  else if (color == TFTCYAN) c = TFT_CYAN;
  else if (color == TFTBLUE) c = TFT_BLUE;
  else if (color == TFTRED) c = TFT_RED;
  else if (color == TFTGREEN) c = TFT_GREEN;
  else if (color == TFTGREY) c = TFT_LIGHTGREY;
  else if (color == TFTGRAY) c = TFT_LIGHTGREY;
  else if (color == TFTMAGENTA) c = TFT_MAGENTA;
  else if (color == TFTVIOLET) c = TFT_VIOLET;
  else if (color == TFTORANGE) c = TFT_ORANGE;
  else if (color == TFTYELLOW) c = TFT_YELLOW;
  else if (color == TFTLIGHTGREY) c = TFT_LIGHTGREY;
  else if (color == TFTPURPLE) c = TFT_PURPLE;
  else if (color == TFTNAVY) c = TFT_NAVY;
  else if (color == TFTSILVER) c = TFT_SILVER;
  else if (color == TFTDARKGREY) c = TFT_DARKGREY;
  else if (color == TFTSKYBLUE) c = TFT_SKYBLUE;
  else if (color == TFTLIME) c = 0x97e0;
  else c = color;

  // Theme remap (menus only). Keep every label readable against the bg.
  if (dark_mode) {
    // Lift dark colors off a black background.
    switch (c) {
      case TFT_NAVY:     return TFT_SKYBLUE;
      case TFT_BLUE:     return TFT_SKYBLUE;
      case TFT_PURPLE:   return TFT_PINK;
      case TFT_VIOLET:   return TFT_PINK;
      case TFT_DARKGREY: return TFT_LIGHTGREY;
      default:           return c;
    }
  } else {
    // Light mode: push light colors down to dark, readable on white.
    switch (c) {
      case TFT_WHITE:     return TFT_BLACK;
      case TFT_YELLOW:    return TFT_OLIVE;
      case TFT_CYAN:      return TFT_NAVY;
      case TFT_SKYBLUE:   return TFT_NAVY;
      case TFT_GREEN:     return TFT_DARKGREEN;
      case 0x97e0:        return TFT_DARKGREEN;   // lime
      case TFT_LIGHTGREY: return TFT_DARKGREY;
      case TFT_SILVER:    return TFT_DARKGREY;
      case TFT_ORANGE:    return TFT_MAROON;
      default:            return c;               // red/magenta/navy/purple/blue already dark
    }
  }
}

// Function to change menu
void MenuFunctions::changeMenu(Menu * menu)
{
  display_obj.initScrollValues();
  display_obj.setupScrollArea(TOP_FIXED_AREA, BOT_FIXED_AREA);
  // Was display_obj.tft.init() — a full ST7796 re-init (with ~120ms of built-in
  // sleep-out delays) on every menu change, which caused the slow "fade". The
  // panel is already initialised at boot; only the rotation needs restoring
  // after screens that switch to landscape (e.g. WiFi scans).
  display_obj.tft.setRotation(0);
  current_menu = menu;

  current_menu->selected = 0;
  
  buildButtons(menu);

  displayCurrentMenu();
}

// Tap-4-corners touch recalibration. Fixes the wrong stock calibration (X was
// flipped/compressed) so the arrows and everything else land where you tap.
void MenuFunctions::runTouchCalibration() {
  TFT_eSPI &tft = display_obj.tft;
  tft.setRotation(0);              // same rotation the menus use
  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(NULL);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Touch each corner", tft.width() / 2, tft.height() / 2 - 24);
  tft.drawString("as the arrow shows", tft.width() / 2, tft.height() / 2 + 4);
  delay(1600);

  tft.calibrateTouch(touch_cal, TFT_MAGENTA, TFT_BLACK, 20);  // interactive
  tft.setTouch(touch_cal);
  saveTouchCal();

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Calibrated", tft.width() / 2, tft.height() / 2);
  delay(1200);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  this->changeMenu(&settingsMenu);  // back to settings, freshly drawn
}

// Horizontally scroll (marquee) any visible menu label wider than the row. Uses
// an off-screen sprite pushed in one shot, so there's no clear-then-draw flicker
// (the trap the vertical scroll fell into). Only over-long labels are touched;
// everything else keeps its static text.
void MenuFunctions::updateMarquees() {
  if (wifi_scan_obj.currentScanMode != WIFI_SCAN_OFF) return;   // menus only
  if (current_menu == NULL || current_menu->list == NULL) return;

  static Menu* mq_menu = NULL;
  static int mq_start = -1;
  static uint32_t mq_tick = 0;
  static int32_t mq_offset = 0;

  // Restart the scroll when the menu or scroll position changes.
  if (current_menu != mq_menu || this->menu_start_index != mq_start) {
    mq_menu = current_menu;
    mq_start = this->menu_start_index;
    mq_offset = 0;
  }

  uint32_t now = millis();
  if (now - mq_tick < 45) return;   // ~22 steps/sec
  mq_tick = now;
  mq_offset += 2;                    // pixels per step

  int size = current_menu->list->size();
  bool scrollable = size > BUTTON_SCREEN_LIMIT;
  int region_x = BUTTON_PADDING;
  // Non-scrollable: fill right up to x=TFT_WIDTH-1 so the scroll region butts
  // against the border at the far edge (no stray static-label pixel in the gap).
  // The border itself (x=0 / TFT_WIDTH-1) is redrawn on top in displayCurrentMenu
  // and never touched here, so it stays crisp as the label scrolls.
  int region_right = scrollable ? (SCROLL_ARROW_X - 4) : (TFT_WIDTH - 1);
  int region_w = region_right - region_x;
  if (region_w < 24) return;

  int sprite_h = KEY_H - 2;                 // inside the top/bottom outlines
  int text_y = KEY_H / 2 - 3;               // match drawButton's vertical placement
  uint16_t bg = dark_mode ? TFT_BLACK : TFT_WHITE;

  display_obj.tft.setFreeFont(MENU_FONT);   // for textWidth() below

  TFT_eSprite spr = TFT_eSprite(&display_obj.tft);
  spr.setColorDepth(16);
  if (!spr.createSprite(region_w, sprite_h)) return;   // out-of-memory guard
  spr.setFreeFont(MENU_FONT);
  spr.setTextDatum(ML_DATUM);

  int visible = min(size - this->menu_start_index, (int)BUTTON_SCREEN_LIMIT);
  const int GAP = 34;   // gap before the label repeats, for a clean loop

  for (int b = 0; b < visible; b++) {
    int idx = b + this->menu_start_index;
    String name = current_menu->list->get(idx).name;
    if (name == text09) continue;                        // "Back"
    int tw = display_obj.tft.textWidth(name);
    if (tw <= region_w) continue;                        // fits: leave static text

    uint16_t txt = this->itemColor(current_menu, idx);
    int button_center = KEY_Y + b * (KEY_H + KEY_SPACING_Y);
    int sprite_y = button_center - KEY_H / 2 + 1;

    spr.fillSprite(bg);
    spr.setTextColor(txt, bg);
    int period = tw + GAP;
    int ox = -((int)(mq_offset % period));
    spr.drawString(name, ox, text_y);
    spr.drawString(name, ox + period, text_y);          // second copy loops seamlessly
    spr.pushSprite(region_x, sprite_y);
    this->redrawButtonBorder(b, false);   // keep the right-edge corners crisp under the scroll
  }
  spr.deleteSprite();
}

// Draw the up/down arrows on the right edge, but only when the menu overflows.
// Greyed when you're already at that end.
void MenuFunctions::drawScrollArrows() {
  if (current_menu == NULL || current_menu->list == NULL) return;
  if ((int)current_menu->list->size() <= BUTTON_SCREEN_LIMIT) return;
  uint16_t bg  = dark_mode ? TFT_BLACK : TFT_WHITE;
  uint16_t on  = dark_mode ? TFT_WHITE : TFT_BLACK;  // enabled: bright, high-contrast
  uint16_t off = SCROLL_DISABLED_GREY;                // disabled: clearly greyed-out (~30% darker)
  const int x = SCROLL_ARROW_X, w = SCROLL_ARROW_W, h = SCROLL_ARROW_H;
  bool canUp = this->menu_start_index > 0;
  bool canDn = this->menu_start_index < (int)current_menu->list->size() - BUTTON_SCREEN_LIMIT;
  uint16_t upCol = canUp ? on : off;   // box border + arrow both dim when you can't scroll
  uint16_t dnCol = canDn ? on : off;

  display_obj.tft.fillRect(x, SCROLL_UP_Y, w, h, bg);
  display_obj.tft.drawRect(x, SCROLL_UP_Y, w, h, upCol);
  display_obj.tft.fillTriangle(x + w / 2, SCROLL_UP_Y + 7,
                               x + 6, SCROLL_UP_Y + h - 8,
                               x + w - 6, SCROLL_UP_Y + h - 8,
                               upCol);

  display_obj.tft.fillRect(x, SCROLL_DN_Y, w, h, bg);
  display_obj.tft.drawRect(x, SCROLL_DN_Y, w, h, dnCol);
  display_obj.tft.fillTriangle(x + w / 2, SCROLL_DN_Y + h - 7,
                               x + 6, SCROLL_DN_Y + 7,
                               x + w - 6, SCROLL_DN_Y + 7,
                               dnCol);
}

// Move the visible window of a long menu by `delta` rows and redraw. Clamped so
// it can never scroll past either end (and never index the button array OOB).
void MenuFunctions::scrollMenu(int delta) {
  if (delta == 0 || current_menu == NULL || current_menu->list == NULL)
    return;
  int size = current_menu->list->size();
  int maxStart = size - BUTTON_SCREEN_LIMIT;
  if (maxStart < 0) maxStart = 0;               // menu fits on screen; nothing to scroll
  int newStart = this->menu_start_index + delta;
  if (newStart < 0) newStart = 0;
  if (newStart > maxStart) newStart = maxStart;
  if (newStart == this->menu_start_index)
    return;
  this->buildButtons(current_menu, newStart);   // re-inits key[0..visible-1] for the new slice
  this->displayCurrentMenu(newStart);           // full redraw (also sets menu_start_index)
}

void MenuFunctions::buildButtons(Menu *menu, int starting_index, String button_name) {
  if (menu->list == NULL || menu->list->size() == 0)
      return;

  // Ensure starting index is within bounds
  if (starting_index >= menu->list->size())
    starting_index = menu->list->size() - BUTTON_SCREEN_LIMIT;
  if (starting_index < 0)
    starting_index = 0;

  this->menu_start_index = starting_index;

  // Determine the number of buttons to display (limited to screen capacity)
  uint8_t visible_buttons = min(BUTTON_SCREEN_LIMIT, menu->list->size() - starting_index);

  // Loop through and create only the visible buttons
  for (uint8_t i = 0; i < visible_buttons; i++) {
    uint16_t color = this->itemColor(menu, starting_index + i);

    char buf[menu->list->get(starting_index + i).name.length() + 1] = {};
    if (button_name != "")
      menu->list->get(starting_index + i).name.toCharArray(buf, menu->list->get(starting_index + i).name.length() + 1);
    else
      button_name.toCharArray(buf, button_name.length() + 1);

    if (i >= BUTTON_SCREEN_LIMIT) {
      Serial.println("Error: Trying to access out-of-bounds button index " + (String)i);
      break;
    }

    display_obj.key[i].initButton(&display_obj.tft,
                                  KEY_X + 0 * (KEY_W + KEY_SPACING_X),
                                  KEY_Y + i * (KEY_H + KEY_SPACING_Y), // Positioning buttons vertically
                                  KEY_W,
                                  KEY_H,
                                  this->themeOutline(),              // Outline (theme: white/black/hacker-grey)
                                  dark_mode ? TFT_BLACK : TFT_WHITE, // Fill (theme background)
                                  color, // Text color (theme-remapped in getColor)
                                  buf,
                                  KEY_TEXTSIZE);

    
    // drawButton draws at _h/2 - 4 + this offset. 2 -> text sits 2px above centre,
    // so descenders (g/p/y/q/j) clear the bottom outline; the top has ample room.
    display_obj.key[i].setLabelDatum(BUTTON_PADDING - (KEY_W / 2), 2, ML_DATUM);
  }
}

// The icon is drawn at x=0 (covers the button's LEFT border) and drawButton
// paints the label AFTER its own outline (a long label overruns the RIGHT
// border). Repaint the rounded outline on top so both side edges stay crisp.
// The marquee sprite only touches x=BUTTON_PADDING..region_right, never x=0 or
// the right edge, so a border redrawn here survives the scroll.
void MenuFunctions::redrawButtonBorder(int b, bool pressed) {
  (void)pressed;   // TFT_eSPI_Button keeps _outlinecolor on invert (only fill/text swap),
                   // so the border is the same colour pressed or not.
  int x1 = KEY_X - (KEY_W / 2);
  int y1 = (KEY_Y + b * (KEY_H + KEY_SPACING_Y)) - (KEY_H / 2);
  int r  = min((int)KEY_W, (int)KEY_H) / 4;   // matches TFT_eSPI_Button's corner radius
  uint16_t bc = this->themeOutline();         // == the outline colour passed to initButton
  display_obj.tft.drawRoundRect(x1, y1, KEY_W, KEY_H, r, bc);
}

void MenuFunctions::displayCurrentMenu(int start_index)
{
  // Clear the screen before displaying the current menu
  display_obj.clearScreen();
  // Re-seed the Hacker rain columns (staggered above the top) for this menu.
  if (ui_theme == THEME_HACKER) {
    for (int c = 0; c < 64; c++) hrain_drop[c] = -(int)random(0, 30);
    hrain_tick = 0;
  }
  // Light theme: white menu body (the banner/status bar stay dark on purpose,
  // giving a dark-header/light-body look; scan screens are untouched).
  if (!dark_mode)
    display_obj.tft.fillScreen(TFT_WHITE);
  display_obj.updateBanner(current_menu->name);
  display_obj.tft.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
  this->drawStatusBar();

  if (current_menu->list != NULL)
  {
    #ifdef HAS_FULL_SCREEN
      display_obj.tft.setFreeFont(MENU_FONT);
    #endif

    #ifdef HAS_MINI_SCREEN
      display_obj.tft.setFreeFont(NULL);
      display_obj.tft.setTextSize(1);
    #endif
    for (uint16_t i = start_index; i < min(start_index + BUTTON_SCREEN_LIMIT, current_menu->list->size()); i++)
    {
      if (!current_menu || !current_menu->list || i >= current_menu->list->size())
        continue;
      uint16_t color = this->itemColor(current_menu, i);
      #ifdef HAS_FULL_SCREEN
        #if !defined(HAS_ILI9341) && !defined(HAS_ST7796) && !defined(HAS_ST7789)
          if ((current_menu->list->get(i).selected) || (current_menu->selected == i)) {
            display_obj.key[i - start_index].drawButton(true, current_menu->list->get(i).name);
          }
          else {
            display_obj.key[i].drawButton(false, current_menu->list->get(i).name);
          }
        #else
          // buildButtons() fills key[0..visible-1] (relative), so draw with the
          // relative index too — otherwise scrolled menus read the wrong/blank
          // button slot and crash. list->get(i) stays absolute (correct item).
          display_obj.key[i - start_index].drawButton(false, current_menu->list->get(i).name);
        #endif

        this->drawItemIcon(current_menu, i,
                           KEY_Y + (i - start_index) * (KEY_H + KEY_SPACING_Y) - (ICON_H / 2),
                           color);   // normal icon, or a Hacker/Pride glyph

        // Repaint the outline over the icon (left) and any overrunning label (right).
        this->redrawButtonBorder(i - start_index, false);

      #endif

      #ifdef HAS_MINI_SCREEN
        if ((current_menu->selected == i) || (current_menu->list->get(i).selected))
          display_obj.key[i - start_index].drawButton(true, current_menu->list->get(i).name);
        else
          display_obj.key[i - start_index].drawButton(false, current_menu->list->get(i).name);
      #endif
    }
    display_obj.tft.setFreeFont(NULL);
    this->drawScrollArrows();   // ▲▼ on the right edge when the menu overflows
  }
}

#endif



