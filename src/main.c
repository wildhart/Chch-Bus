#include "pebble.h"

// include these two lines in final build to remove logging
//#undef APP_LOG
//#define APP_LOG(level, fmt, args... )
  
#define LOG_HEAP(text) APP_LOG(APP_LOG_LEVEL_INFO, "heap: %d, used: %d, free: %d, %s %s",  heap_bytes_used()+heap_bytes_free(), heap_bytes_used(), heap_bytes_free(), __func__, text)

static Window *window_menu;
#ifdef PBL_SDK_3
static StatusBarLayer *s_status_bar;
#endif
static MenuLayer *menu_layer;
enum { // main menu structure
  MENU_SECTION_FAVS,
  MENU_SECTION_ADD,
  MENU_SECTION_OPTIONS,
  MENU_SECTION_REMOVE,
  
  NUM_MENU_SECTIONS,
  
  MENU_ADD_NUMBER=MENU_SECTION_ADD*100,
  MENU_ADD_LOCATION,
  NUM_MENU_ITEMS_ADD=2,
  
  MENU_OPTIONS_DISTANCE=MENU_SECTION_OPTIONS*100,
  MENU_OPTIONS_AUTO,
  MENU_OPTIONS_ROUTES,
  NUM_MENU_ITEMS_OPTIONS=3,
};
#define MENU_SECTION_CELL (cell_index->section * 100 + cell_index->row)
#define MENU_HEIGHT_SINGLE 30
#define MENU_HEIGHT_DOUBLE 44
#define MAX_PLATFORMS 8
#define MAX_PL_NUM_LENGTH 6
#define MAX_NAME_LENGTH 32
#define MAX_ROAD_LENGTH 32
struct platform {
  char Number[MAX_PL_NUM_LENGTH+1];
  char Name[MAX_NAME_LENGTH+1];
  char Road[MAX_ROAD_LENGTH+1];
} platforms[MAX_PLATFORMS]; 
static uint8_t num_platforms = 0;
static int8_t platform = -1;

static Window *window_list;
static ScrollLayer *scroll_layer;
static Layer *arrivals_layer;
#define PLATFORM_WIDTH 30
#define PLATFORM_HEIGHT 12
#define ARRIVAL_HEIGHT 16
#define ROUTE_WIDTH 24
#define MAX_ARRIVALS 30
#define MAX_ROUTE_LENGTH 4
#define MAX_DESTINATION_LENGTH 32
static bool fetching = false;
static uint8_t retries = 0;
static struct arrival {
  char Route[MAX_ROUTE_LENGTH+1];
  char Destination[MAX_DESTINATION_LENGTH+1];
  uint8_t Eta;
  uint16_t Trip;
  bool Favourite;
  bool Visible;
} arrivals[MAX_ARRIVALS];
static uint8_t num_arrivals=0;
static uint8_t num_arrivals_favourites=0;
static uint8_t num_arrivals_visible=0;
static int8_t single_arrival_visible_index=-1;
static int8_t arrival_selected;
static uint8_t arrival_selected_index;
static uint16_t trip_selected=0;
static bool show_time=false;
static struct early_warning_st {
  uint Platform;
  uint Trip;
  uint Mins;
} early_warning;

static Window *window_menu_nearest;
static MenuLayer *menu_layer_nearest;
#define MAX_SHORT_DESTINATION_LENGTH 14
static struct nearest {
  char Number[MAX_PL_NUM_LENGTH+1];
  char Name[MAX_NAME_LENGTH+1];
  char Road[MAX_ROAD_LENGTH+1];
} nearest[MAX_PLATFORMS]; 
static uint8_t num_nearest=0;
static struct route {
  char Route[MAX_ROUTE_LENGTH+1];
  char Destination[MAX_SHORT_DESTINATION_LENGTH+1];
} *routes;
static int num_routes=0;

static Window *window_number;
static char number_selector[6] = "53088"; // Central station - changed 29/5/15
static Layer *number_layer;
static ActionBarLayer *action_bar_layer;
static GBitmap *bitmap_up;
static GBitmap *bitmap_down;
static GBitmap *bitmap_right;
static GBitmap *bitmap_tick;
static GBitmap *bitmap_option_tick;
static GBitmap *bitmap_option_box;
static GBitmap *bitmap_bell;
static GBitmap *bitmap_refresh;
static uint8_t number_selected = 0;
bool valid_selector;
#define NUM_DIGITS 5
#define NUMBER_ORIGIN 2 // Action bar layer =20px wide on Aplite =30px on Basalt, use ACTION_BAR_WIDTH 
#define OPTION_TICK_SIZE 16

static Window *window_distance;
static Layer *distance_layer;
static ActionBarLayer *distance_action_bar_layer;
static uint8_t distance_selection=0;
static bool distance_refreshing=false;
static uint8_t platforms_distances[MAX_PLATFORMS];
static struct distance_st {
  uint8_t Platform_index;
  uint8_t km;
} distance_alarm;
  
// Key values for AppMessage Dictionary
#define KEY_ARRIVALS 0
#define KEY_LOCATION 1
#define KEY_CHECK_PLATFORM 2
#define KEY_JS_READY 3
#define KEY_NEAREST_FAV 4
#define KEY_SAVE_SETTINGS 5
#define KEY_GET_ROUTES 6
#define KEY_DISTANCE_FAV 7
#define KEY_ACK 8
static bool js_ready = false;
static bool js_outbox_waiting = false;
static uint8_t last_message;
static uint16_t outbox_size;
static void (*next_message_callback)(void);
  
// Persistent Storage Keys
#define STORAGE_KEY_AUTOSELECT 1  
#define STORAGE_KEY_VERSION 2
#define STORAGE_KEY_FAVOURITE_ROUTES_SHOW 3
#define STORAGE_KEY_FAVOURITE_ROUTES_LIST 4
#define STORAGE_KEY_EARLY_WARNING 5
#define STORAGE_KEY_HELP_FLAGS 6
#define STORAGE_KEY_DISTANCE_ALARM 7
#define STORAGE_KEY_PLATFORM 100
#define CURRENT_STORAGE_VERSION 2
static bool autoselect = false;
static bool favourite_routes_show = false;
#define MAX_FAV_ROUTES_LIST_LENGTH 32
static char favourite_routes_list[MAX_FAV_ROUTES_LIST_LENGTH+1];
static bool data_exists = false;
static AppTimer *timer=0; // this is for regular fetching of arrivals
static WakeupId wakeup_id; // this is for relaunching the app when a bus or stop gets near

#define HELP_FLAGS_ETA    1
#define HELP_FLAGS_DOWN   2
#define HELP_FLAGS_FULLSCREEN  4
#define HELP_FLAGS_ALARM  8
#define HELP_FLAGS_DISTANCE 16
static uint8_t help_flags=0;
static uint8_t old_help_flags=0;

// *****************************************************************************************************
// MESSAGES
// *****************************************************************************************************

static void redraw_arrivals() {
  // Set correct size of arrivals list and redraw  

  single_arrival_visible_index=-1;
  if (trip_selected) {
    for (int a=0; a<num_arrivals; a++) {
      arrivals[a].Visible = arrivals[a].Trip==trip_selected;
    }
    num_arrivals_visible=1;
  } else if (favourite_routes_show && num_arrivals_favourites) {
    for (int a=0; a<num_arrivals; a++) {
      arrivals[a].Visible = arrivals[a].Favourite;
    }
    num_arrivals_visible=num_arrivals_favourites;
  } else {
    for (int a=0; a<num_arrivals; a++) arrivals[a].Visible=true;
    num_arrivals_visible=num_arrivals;
  }
  
  GRect bounds = layer_get_frame(scroll_layer_get_layer(scroll_layer));
  uint height=ARRIVAL_HEIGHT+ARRIVAL_HEIGHT*num_arrivals_visible;
  if (height<168-PLATFORM_HEIGHT-16) height=168-PLATFORM_HEIGHT-16;
  scroll_layer_set_content_size(scroll_layer, GSize(bounds.size.w, PLATFORM_HEIGHT+height));
  scroll_layer_set_content_offset(scroll_layer, GPoint(0,0), true);
  layer_set_frame(arrivals_layer, GRect(0,0, bounds.size.w, PLATFORM_HEIGHT+height));
  layer_mark_dirty(scroll_layer_get_layer(scroll_layer));
}

static void send_message_when_phone_ready() {
  if (js_ready) app_message_outbox_send(); else js_outbox_waiting=true;
}

// Write message to buffer & send
static void fetch_arrivals() {
  if (platform>=0) {
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    char text[20];
    
    snprintf(text,20,"%d;%s", outbox_size, platforms[platform].Number);
    //APP_LOG(APP_LOG_LEVEL_INFO, "Sending Platform %s.", platforms[platform].Number);
    dict_write_cstring(iter, last_message=KEY_ARRIVALS, text);
   
    send_message_when_phone_ready();
    fetching = true;
    retries++;
    redraw_arrivals();
    
    // set timer for next fetch.
    timer = app_timer_register(30000, fetch_arrivals, NULL); // metro info API data is only updated every 30 seconds.
  }
}

static char* check_if_favourite_route(char *route) {
  char rte[MAX_ROUTE_LENGTH+3];
  snprintf(rte,MAX_ROUTE_LENGTH+3,",%s,",route);
  return strstr(favourite_routes_list,rte); // pointer to substring if true, NULL if false
}

static void set_wakeup(uint reason, uint mins) {
  if (wakeup_id>0) {
    wakeup_cancel(wakeup_id);
    wakeup_id=0;
  }
  if (mins) {
    wakeup_id = wakeup_schedule(time(NULL)+60*mins,reason,false);
    if (wakeup_id==E_RANGE) set_wakeup(reason,mins-1);
  }
  //APP_LOG(APP_LOG_LEVEL_INFO, "set wakeup id:%d, reason:%d, mins:%d", (int)wakeup_id,reason,mins);
}

static void set_early_warning(const uint platform, const uint trip, uint mins, uint eta) {
  early_warning.Platform=mins ? platform : 0;
  early_warning.Trip=mins ? trip : 0;
  early_warning.Mins=mins;
  if (mins==0) { // cancel wakeup timer
    persist_delete(STORAGE_KEY_EARLY_WARNING);
    set_wakeup(0,0);
  } else { // set wakeup timer
    persist_write_data(STORAGE_KEY_EARLY_WARNING, &early_warning, sizeof(early_warning));
    mins = eta-mins;
    set_wakeup(STORAGE_KEY_EARLY_WARNING, (mins<5) ? 1 : mins/2);
  }
}

static void set_distance_warning() {
  if (distance_alarm.km==0) {// cancel wakeup timer
    persist_delete(STORAGE_KEY_DISTANCE_ALARM);
    set_wakeup(0,0);
  } else {
    persist_write_data(STORAGE_KEY_DISTANCE_ALARM, &distance_alarm, sizeof(distance_alarm));
    int mins = 60 * (platforms_distances[distance_alarm.Platform_index] - distance_alarm.km) / 30; // assume bus travelling at 30 km/h
    set_wakeup(STORAGE_KEY_DISTANCE_ALARM, (mins<5) ? 1 : mins/2);
  }
}

static void vibrate() {
  // Vibe pattern: ON for 200ms, OFF for 100ms, ON for 400ms:
  static const uint32_t segments[] = { 400,200, 400,200, 400,200, 400,200, 400 };
  VibePattern pat = {
    .durations = segments,
    .num_segments = ARRAY_LENGTH(segments),
  };
  vibes_enqueue_custom_pattern(pat);
}

static void process_arrivals(char *source) {
  uint columns=4;
  char bus[columns][MAX_NAME_LENGTH+1];
  uint c=0; // column (0=route, 1=destination, 2 = eta)
  bool selected_trip_still_visible = false;
  num_arrivals=0;
  num_arrivals_favourites=0;
  arrival_selected=-1;
  while (*source && num_arrivals<MAX_ARRIVALS) {
    uint d=0; // destination offset
    while (*source && *source!=';' && d<MAX_NAME_LENGTH) {
      bus[c][d++]=*source++;
    }
    while (*source && *source!=';') source++;
    bus[c++][d]=0;
    source++;
    if (c==columns) {
      strncpy(arrivals[num_arrivals].Route, bus[0], MAX_ROUTE_LENGTH);
      strncpy(arrivals[num_arrivals].Destination, bus[1], MAX_DESTINATION_LENGTH);
      arrivals[num_arrivals].Eta=atoi(bus[2]);
      arrivals[num_arrivals].Trip=atoi(bus[3]);
      if (arrivals[num_arrivals].Trip==early_warning.Trip) {
        trip_selected=early_warning.Trip; // switch back to correct trip for early warning.
        if (early_warning.Mins>=arrivals[num_arrivals].Eta) { // VIBRATE !!!!!
           vibrate();
        } else if (!persist_exists(STORAGE_KEY_EARLY_WARNING)) {
          set_early_warning(platform, trip_selected, early_warning.Mins, arrivals[num_arrivals].Eta);
        }
      }
      if (arrivals[num_arrivals].Trip==trip_selected) {
        selected_trip_still_visible=true;
      }
      // APP_LOG(APP_LOG_LEVEL_INFO, "route:%s; dest:%s; eta:%s", arrivals[num_arrivals].Route,arrivals[num_arrivals].Destination,arrivals[num_arrivals].Eta);
      arrivals[num_arrivals].Favourite = check_if_favourite_route(arrivals[num_arrivals].Route);
      if (arrivals[num_arrivals].Favourite) num_arrivals_favourites++; // there is a favourite, so only show favourites
      num_arrivals++;
      c=0;
    }
  }
  if (!selected_trip_still_visible) trip_selected=0;
  fetching=false;
  retries=0;
  redraw_arrivals();
}

static void fetch_favourite_distances() {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  //char plats[(MAX_PL_NUM_LENGTH+1)*MAX_PLATFORMS+6];
  char plats[100];
  uint len=0;
  len+=snprintf(plats,10,"alarm|");
  for (uint a=0; a<num_platforms; a++) len+=snprintf(plats+len,100-len,"%s;",platforms[a].Number);
  
  dict_write_cstring(iter, last_message=KEY_LOCATION, plats);
 
  send_message_when_phone_ready();
  distance_refreshing=true;
  layer_mark_dirty(distance_layer);
  timer = app_timer_register(30000, fetch_favourite_distances, NULL); // get location every 30 seconds
}

static void process_distances(char *source) {
  uint columns=1;
  char bus[columns][MAX_NAME_LENGTH+1];
  uint8_t c=0; // column
  uint8_t num=0;
  uint d; // destination offset
  while (*source) {
    d=0;
    while (*source && *source!=';' && d<MAX_NAME_LENGTH) {
      bus[c][d++]=*source++;
    }
    while (*source && *source!=';') source++;
    bus[c++][d]=0;
    source++;
    if (c==columns) {
      platforms_distances[num] = atoi(bus[0]);
      if (distance_alarm.km && num==distance_alarm.Platform_index && platforms_distances[num]<=distance_alarm.km) vibrate();
      num++;
      c=0;
    }
  }
  distance_refreshing=false;
  layer_mark_dirty(distance_layer);
  if (!persist_exists(STORAGE_KEY_DISTANCE_ALARM)) set_distance_warning();
}

static void fetch_nearest_platforms(char *favourites) {
  #define get_nearest_favourite favourites[0]
    
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  Tuplet value = get_nearest_favourite ? TupletCString(last_message=KEY_LOCATION, favourites) : TupletInteger(last_message=KEY_LOCATION, MAX_PLATFORMS);
  dict_write_tuplet(iter, &value);
 
  send_message_when_phone_ready();
  
  if (!get_nearest_favourite) {
    num_nearest=1;
    strcpy(nearest[0].Number, "");
    strcpy(nearest[0].Name, "");
    strcpy(nearest[0].Road, "Getting Location...");
    window_stack_push(window_menu_nearest, true /* Animated */);
  }
}

static void process_platforms(char *source) {
  uint columns=3;
  char bus[columns][MAX_NAME_LENGTH+1];
  uint c=0; // column (0=number, 1=name, 3=road)
  num_nearest=0;
  num_routes=0;
  while (*source && num_arrivals<MAX_ARRIVALS) {
    uint d=0; // destination offset
    while (*source && *source!=';' && d<MAX_NAME_LENGTH) {
      bus[c][d++]=*source++;
    }
    while (*source && *source!=';') source++;
    bus[c++][d]=0;
    source++;
    if (c==columns) {
      strncpy(nearest[num_nearest].Number, bus[0], MAX_PL_NUM_LENGTH);
      strncpy(nearest[num_nearest].Name, bus[1], MAX_NAME_LENGTH);
      strncpy(nearest[num_nearest].Road, bus[2], MAX_ROAD_LENGTH);
      //APP_LOG(APP_LOG_LEVEL_INFO, "number:%s; name:%s; road:%s", nearest[num_nearest].Number,nearest[num_nearest].Name,nearest[num_nearest].Road);
      num_nearest++;
      c=0;
    }
  }
  menu_layer_reload_data(menu_layer_nearest);
}

static void check_platform(char *platform) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  Tuplet value = TupletCString(last_message=KEY_CHECK_PLATFORM, platform);
  dict_write_tuplet(iter, &value);
 
  send_message_when_phone_ready();
}

static void process_check_platform(char *source) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Platform checked: %s", source);
  char bus[3][MAX_NAME_LENGTH+1];
  for (int c=0; c<3; c++) {
    uint d=0; // destination offset
    while (*source && *source!=';' && d<MAX_NAME_LENGTH) {
      bus[c][d++]=*source++;
    }
    while (*source && *source!=';') source++;
    bus[c][d]=0;
    source++;
  }
  num_nearest=0;
  strncpy(nearest[num_nearest].Name, bus[0], MAX_NAME_LENGTH);
  strncpy(nearest[num_nearest].Road, bus[1], MAX_ROAD_LENGTH);
  strncpy(nearest[num_nearest].Number, bus[2], MAX_PL_NUM_LENGTH);
  
  valid_selector=strncmp(nearest[0].Road,"Invalid",6); // Platform 51556 hs no ame
  if (!valid_selector && number_selected==NUM_DIGITS) number_selected=0;
  action_bar_layer_set_icon(action_bar_layer, BUTTON_ID_UP, (valid_selector && number_selected==NUM_DIGITS) ? bitmap_right : bitmap_up);
  action_bar_layer_set_icon(action_bar_layer, BUTTON_ID_DOWN, (valid_selector && number_selected==NUM_DIGITS)? bitmap_right : bitmap_down);
  action_bar_layer_set_icon(action_bar_layer, BUTTON_ID_SELECT, (valid_selector && number_selected==NUM_DIGITS) ? bitmap_tick : bitmap_right);
  layer_mark_dirty(number_layer);
}

static void fetch_routes_for_platforms(char *platforms) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  Tuplet value = TupletCString(last_message=KEY_GET_ROUTES, platforms);
  dict_write_tuplet(iter, &value);
 
  send_message_when_phone_ready();
}

static void process_routes(char *source) {
  const uint columns = 2;
  char bus[columns][MAX_NAME_LENGTH+1];
  uint c=0; // column (0=number, 1=name, 3=road)
  num_routes=0;
  num_nearest=0;
  char *scopy=source;
  // first count number of routes to allocate enough memory.
  while (*scopy) {
    if (*scopy++==';') num_routes++; // this will count twice the number of routes
  }
  
  struct route *r=malloc( (num_routes/columns) * sizeof(struct route)); // num_routes is atually the number of semicolons
  routes = r;
  
  num_routes=0;
  while (*source) {
    uint d=0; // destination offset
    while (*source && *source!=';' && d<MAX_NAME_LENGTH) {
      bus[c][d++]=*source++;
    }
    while (*source && *source!=';') source++;
    bus[c++][d]=0;
    source++;
    if (c==columns) {
      strncpy(r[num_routes].Route, bus[0], MAX_ROUTE_LENGTH);
      strncpy(r[num_routes].Destination, bus[1], MAX_SHORT_DESTINATION_LENGTH);
      //APP_LOG(APP_LOG_LEVEL_INFO, "route:%s; dest:%s;", r[num_routes].Route,r[num_routes].Destination);
      //APP_LOG(APP_LOG_LEVEL_INFO, "route:%s; dest:%s;", bus[0],bus[1]);
      num_routes++;
      c=0;
    }
  }
  menu_layer_reload_data(menu_layer_nearest);
}

static void process_settings(char *source) {
  char bus[4][MAX_NAME_LENGTH+1];
  uint c=0; // column (0=number, 1=name, 3=road)
//  uint32_t version=0;
  num_platforms=0;
  char plats[(MAX_PL_NUM_LENGTH+1)*MAX_PLATFORMS]="";
  
  while (*source) {
    uint d=0; // destination offset
    while (*source && *source!=';' && *source!='|' && d<MAX_NAME_LENGTH) {
      bus[c][d++]=*source++;
    }
    while (*source && *source!=';' && *source!='|') source++;
    bus[c++][d]=0;
    if (*source=='|') {
      int key = atoi(bus[0]);
      if (key>STORAGE_KEY_PLATFORM) key = STORAGE_KEY_PLATFORM;
      switch (key) {
        case STORAGE_KEY_VERSION:     
//          version = atoi(bus[1]);       
          break;
        case STORAGE_KEY_AUTOSELECT:
          persist_write_bool(STORAGE_KEY_AUTOSELECT,autoselect=(atoi(bus[1])==1));
        break;
        case STORAGE_KEY_FAVOURITE_ROUTES_SHOW:
          persist_write_bool(STORAGE_KEY_FAVOURITE_ROUTES_SHOW,favourite_routes_show=(atoi(bus[1])==1));
          break;
        case STORAGE_KEY_FAVOURITE_ROUTES_LIST:
          snprintf(favourite_routes_list,MAX_FAV_ROUTES_LIST_LENGTH,"%s",bus[1]);
          persist_write_string(STORAGE_KEY_FAVOURITE_ROUTES_LIST, favourite_routes_list);
          break;
        case STORAGE_KEY_HELP_FLAGS:
          persist_write_int(STORAGE_KEY_HELP_FLAGS, old_help_flags=help_flags=(atoi(bus[1])));
          break;
        case STORAGE_KEY_PLATFORM:
          snprintf(platforms[num_platforms].Number,MAX_PL_NUM_LENGTH,"%s",bus[1]);
          snprintf(platforms[num_platforms].Name,MAX_NAME_LENGTH,"%s",bus[2]);
          snprintf(platforms[num_platforms].Road,MAX_ROAD_LENGTH,"%s",bus[3]);
          persist_write_data(STORAGE_KEY_PLATFORM+num_platforms, &platforms[num_platforms], sizeof(platforms[num_platforms]));
          strcat(plats,platforms[num_platforms].Number);
          strcat(plats,";");
          num_platforms++;
          break;
        default:
          APP_LOG(APP_LOG_LEVEL_INFO, "unknown setting key:%s; value:%s", bus[0], bus[1]);
          break;
      }
      c=0;
    }
    source++;
  }

  if (num_platforms && autoselect) fetch_nearest_platforms(plats);
  menu_layer_set_selected_index(menu_layer, MenuIndex(0,0), MenuRowAlignTop, false);
  menu_layer_reload_data(menu_layer);
}

static void save_settings_to_phone() {
  DictionaryIterator *iter;
  char message[outbox_size+1];
  uint len=0;
  
  app_message_outbox_begin(&iter);
  len+=snprintf(message+len,outbox_size-len,"%d;%d|",(int)STORAGE_KEY_VERSION, (int)CURRENT_STORAGE_VERSION);
  len+=snprintf(message+len,outbox_size-len,"%d;%d|",(int)STORAGE_KEY_AUTOSELECT, (int)autoselect);
  len+=snprintf(message+len,outbox_size-len,"%d;%d|",(int)STORAGE_KEY_FAVOURITE_ROUTES_SHOW, (int)favourite_routes_show);
  len+=snprintf(message+len,outbox_size-len,"%d;%s|",(int)STORAGE_KEY_FAVOURITE_ROUTES_LIST, favourite_routes_list);
  len+=snprintf(message+len,outbox_size-len,"%d;%d|",(int)STORAGE_KEY_HELP_FLAGS, help_flags);
  for (uint a=0; a<num_platforms && len<outbox_size; a++) {
    len+=snprintf(message+len,outbox_size-len,"%d;%s;%s;%s|",STORAGE_KEY_PLATFORM+a,platforms[a].Number,platforms[a].Name,platforms[a].Road);
  }
  dict_write_cstring(iter, last_message=KEY_SAVE_SETTINGS, message);
 
  send_message_when_phone_ready();
}

// Called when a message is received from PebbleKitJS
static void inbox_received_handler(DictionaryIterator *received, void *context) {
  // Read first item
  Tuple *t = dict_read_first(received);

  // For all items
  while(t != NULL) {
    // Which key was received?
    //APP_LOG(APP_LOG_LEVEL_INFO, "key=%d; data=%s.", (int)t->key, t->value->cstring);
    switch((int)t->key) {
      case KEY_ARRIVALS:        process_arrivals(t->value->cstring); break;
      case KEY_LOCATION:        process_platforms(t->value->cstring); break;
      case KEY_CHECK_PLATFORM:  process_check_platform(t->value->cstring); break;
      case KEY_GET_ROUTES:      process_routes(t->value->cstring); break;
      case KEY_DISTANCE_FAV:    process_distances(t->value->cstring); break;
      case KEY_JS_READY:
        js_ready=true;
        if (js_outbox_waiting) {
          app_message_outbox_send();
          js_outbox_waiting=false;
        }
        if (t->value->cstring[0]!='\0') {
          if (!data_exists) process_settings(t->value->cstring); // Process the settings which were sent from the phone to the watch.
        } else {
          if (data_exists) save_settings_to_phone(); // No settings saved on phone, so send watch settings to phone.
        }
        break;
      case KEY_NEAREST_FAV:
        if (platform==-1) {
          platform = t->value->int16;
          retries = 0;
          window_stack_push(window_list, true /* Animated */);
          fetch_arrivals();
        }
        break;
      default:
        APP_LOG(APP_LOG_LEVEL_ERROR, "Unknown key: %d data:%s", (int)t->key, t->value->cstring);
        break;
    }

    // Look for next item
    t = dict_read_next(received);
  }
}

static char *translate_error(AppMessageResult result) {
  switch (result) {
    case APP_MSG_OK: return "APP_MSG_OK";
    case APP_MSG_SEND_TIMEOUT: return "APP_MSG_SEND_TIMEOUT";
    case APP_MSG_SEND_REJECTED: return "APP_MSG_SEND_REJECTED";
    case APP_MSG_NOT_CONNECTED: return "APP_MSG_NOT_CONNECTED";
    case APP_MSG_APP_NOT_RUNNING: return "APP_MSG_APP_NOT_RUNNING";
    case APP_MSG_INVALID_ARGS: return "APP_MSG_INVALID_ARGS";
    case APP_MSG_BUSY: return "APP_MSG_BUSY";
    case APP_MSG_BUFFER_OVERFLOW: return "APP_MSG_BUFFER_OVERFLOW";
    case APP_MSG_ALREADY_RELEASED: return "APP_MSG_ALREADY_RELEASED";
    case APP_MSG_CALLBACK_ALREADY_REGISTERED: return "APP_MSG_CALLBACK_ALREADY_REGISTERED";
    case APP_MSG_CALLBACK_NOT_REGISTERED: return "APP_MSG_CALLBACK_NOT_REGISTERED";
    case APP_MSG_OUT_OF_MEMORY: return "APP_MSG_OUT_OF_MEMORY";
    case APP_MSG_CLOSED: return "APP_MSG_CLOSED";
    case APP_MSG_INTERNAL_ERROR: return "APP_MSG_INTERNAL_ERROR";
    default: return "UNKNOWN ERROR";
  }
}
// Called when an incoming message from PebbleKitJS is dropped
static void inbox_dropped_handler(AppMessageResult reason, void *context) {	
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped, reason:%s, last:%d",translate_error(reason),last_message);
}

// Called when PebbleKitJS does not acknowledge receipt of a message
static void outbox_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed, reason:%s",translate_error(reason));
  if (next_message_callback) {
    next_message_callback();
    next_message_callback=NULL;
  }
}
static void outbox_sent_handler(DictionaryIterator *sent, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Message received, next_message_callback=%d", next_message_callback ? 1 : 0);
  if (next_message_callback) {
    next_message_callback();
    next_message_callback=NULL;
  }
}

// *****************************************************************************************************
// CUSTOM
// *****************************************************************************************************

static void add_platform(char number[MAX_PL_NUM_LENGTH], char name[MAX_NAME_LENGTH], char road[MAX_ROAD_LENGTH]) {
  if (num_platforms < MAX_PLATFORMS) {
    snprintf(platforms[num_platforms].Number,MAX_PL_NUM_LENGTH,"%s",number);
    snprintf(platforms[num_platforms].Name,MAX_NAME_LENGTH,"%s",name);
    snprintf(platforms[num_platforms].Road,MAX_ROAD_LENGTH,"%s",road);
    // save platform
    persist_write_data(STORAGE_KEY_PLATFORM+num_platforms, &platforms[num_platforms], sizeof(platforms[num_platforms]));
    num_platforms++;
    save_settings_to_phone();
  }
}
// *****************************************************************************************************
// MENUS
// *****************************************************************************************************

// A callback is used to specify the amount of sections of menu items
// With this, you can dynamically add and remove sections
static uint16_t menu_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  return NUM_MENU_SECTIONS;
}

// Each section has a number of items;  we use a callback to specify this
// You can also dynamically add and remove items using this
static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  switch (section_index) {
    case MENU_SECTION_FAVS:
    case MENU_SECTION_REMOVE: return num_platforms;
    case MENU_SECTION_ADD: return num_platforms<MAX_PLATFORMS ? NUM_MENU_ITEMS_ADD : 0;
    case MENU_SECTION_OPTIONS: return NUM_MENU_ITEMS_OPTIONS;
    default:
      return 0;
  }
}

// A callback is used to specify the height of the section header
static int16_t menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  // This is a define provided in pebble.h that you may use for the default height
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

// A callback is used to specify the height of each menu cell
static int16_t menu_get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (cell_index->section == MENU_SECTION_ADD) {
    return MENU_HEIGHT_SINGLE;
  }
  return MENU_HEIGHT_DOUBLE;
}

// Here we draw what each header is
static void menu_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  // Determine which section we're working with
  switch (section_index) {
    case MENU_SECTION_FAVS:  menu_cell_basic_header_draw(ctx, cell_layer, "Favourite stops"); break;
    case MENU_SECTION_ADD:  menu_cell_basic_header_draw(ctx, cell_layer, "Add stop"); break;
    case MENU_SECTION_OPTIONS:  menu_cell_basic_header_draw(ctx, cell_layer, "Options"); break;
    case MENU_SECTION_REMOVE:  menu_cell_basic_header_draw(ctx, cell_layer, "Remove stop"); break;
  }
}

static void menu_cell_draw_platform(GContext* ctx, /* MenuLayer *menu_layer, MenuIndex *cell_index,*/ const Layer *cell_layer, char *road, char *name) {
  
  GRect bounds = layer_get_frame(cell_layer);
  char road_only[MAX_ROAD_LENGTH+1];
  strncpy(road_only,road,MAX_ROAD_LENGTH);
  char *heading=strstr(road_only," -");
  if (heading) {
    *heading='\0';
    heading+=3;
  }
    
  GFont font_24_bold = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont font_18 = fonts_get_system_font(FONT_KEY_GOTHIC_18);
    
#ifdef PBL_SDK_2
  graphics_context_set_text_color(ctx, GColorBlack);
#endif
  graphics_draw_text(ctx, road_only, font_24_bold, GRect(4, -4, bounds.size.w-8-20, 4+18), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  if (heading) graphics_draw_text(ctx, heading  , font_24_bold, GRect(bounds.size.w-30-4, -4, 30, 4+18), GTextOverflowModeFill, GTextAlignmentRight, NULL);
  graphics_draw_text(ctx, name     , font_18, GRect(4, 20, bounds.size.w-8, 14), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

// This is the menu item draw callback where you specify what each item should look like
static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  // Determine which section we're going to draw in
  switch (cell_index->section) {
    case MENU_SECTION_FAVS:
    case MENU_SECTION_REMOVE:
      // menu_cell_basic_draw(ctx, cell_layer, platforms[cell_index->row].Road, platforms[cell_index->row].Name, NULL);
      menu_cell_draw_platform(ctx, cell_layer, platforms[cell_index->row].Road, platforms[cell_index->row].Name);
      break;

    default:
      switch (MENU_SECTION_CELL) {
        case MENU_ADD_NUMBER: menu_cell_basic_draw(ctx, cell_layer, "By Number", NULL, NULL); break;
        case MENU_ADD_LOCATION: menu_cell_basic_draw(ctx, cell_layer, "By Location", NULL, NULL); break;
        case MENU_OPTIONS_AUTO: menu_cell_basic_draw(ctx, cell_layer, "Auto Select", "Nearest Favourite", autoselect ? bitmap_option_tick : bitmap_option_box); break;
        case MENU_OPTIONS_ROUTES: menu_cell_basic_draw(ctx, cell_layer, "Fav routes",num_platforms?"Long click to edit":"Add fav stop first", favourite_routes_show ? bitmap_option_tick : bitmap_option_box); break;
        case MENU_OPTIONS_DISTANCE: menu_cell_basic_draw(ctx, cell_layer, "Wake me up when", "bus stop approaches", NULL); break;
      }
  }
}

// Here we capture when a user selects a menu item
static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  //LOG_HEAP("");
  switch (cell_index->section) {
    case MENU_SECTION_FAVS: // select favourite
      platform = cell_index->row;
      retries = 0;
      window_stack_push(window_list, true /* Animated */);
      fetch_arrivals();
      break;
    case MENU_SECTION_REMOVE: // delete favourite
      for (uint8_t a=cell_index->row; (int)a<num_platforms-1; a++) {
        platforms[a]=platforms[a+1];
        persist_write_data(STORAGE_KEY_PLATFORM+a, &platforms[a], sizeof(platforms[a]));
      }
      num_platforms--;
      persist_delete(STORAGE_KEY_PLATFORM+num_platforms);
      menu_layer_reload_data(menu_layer);
      save_settings_to_phone();
      break;
    default:
      switch (MENU_SECTION_CELL) {
        case MENU_ADD_NUMBER: window_stack_push(window_number, true /* Animated */); break;
        case MENU_ADD_LOCATION: fetch_nearest_platforms(""); break;
        case MENU_OPTIONS_AUTO: 
        case MENU_OPTIONS_ROUTES: 
          if (MENU_SECTION_CELL==MENU_OPTIONS_AUTO) {
            persist_write_bool(STORAGE_KEY_AUTOSELECT, autoselect=!autoselect);
          } else {
            persist_write_bool(STORAGE_KEY_FAVOURITE_ROUTES_SHOW, favourite_routes_show=!favourite_routes_show);
          }
          menu_layer_reload_data(menu_layer);
          save_settings_to_phone();
          break;
        case MENU_OPTIONS_DISTANCE: 
          window_stack_push(window_distance, true /* Animated */);
      }
  }
}

static void menu_select_long_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (MENU_SECTION_CELL == MENU_OPTIONS_ROUTES) { // Get list of favourte routes for favourite stops
    num_nearest=1;
    strcpy(nearest[0].Number, "");
    strcpy(nearest[0].Name, "for favourite stops...");
    strcpy(nearest[0].Road, "Getting routes");
    window_stack_push(window_menu_nearest, true /* Animated */);
    
    char plats[(MAX_PL_NUM_LENGTH+1)*MAX_PLATFORMS]="";
    for (uint a=0; a<num_platforms; a++) {
      strcat(plats,platforms[a].Number);
      strcat(plats,";");
    }
    fetch_routes_for_platforms(plats);
  }
}

static uint16_t menu_nearest_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  return 1;
}
static uint16_t menu_nearest_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return (KEY_LOCATION==last_message || num_routes==0) ? num_nearest : num_routes;
}
static int16_t menu_nearest_get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  return (KEY_LOCATION==last_message || num_routes==0) ? MENU_HEIGHT_DOUBLE : MENU_HEIGHT_SINGLE;
}
static void menu_nearest_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
   menu_cell_basic_header_draw(ctx, cell_layer, (KEY_LOCATION==last_message ? "Nearest stops" : "Favourite Routes") );
}
static void menu_nearest_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  if (num_nearest==1) {
    // "Getting location" or "Getting routes for favourite stops"
    menu_cell_basic_draw(ctx, cell_layer, nearest[cell_index->row].Road, nearest[cell_index->row].Name, NULL);
  } else if (KEY_LOCATION==last_message) {
    // list of nearest platforms
    menu_cell_draw_platform(ctx, cell_layer, nearest[cell_index->row].Road, nearest[cell_index->row].Name);
  } else { //KEY_GET_ROUTES==last_message
    // list of platforms for favourte stops - display with checkbox.
    GRect bounds = layer_get_frame(cell_layer);
    
    GFont font_24_bold = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    GFont font_24 = fonts_get_system_font(FONT_KEY_GOTHIC_18);
    
#ifdef PBL_SDK_2
  graphics_context_set_text_color(ctx, GColorBlack);
#endif
    graphics_draw_bitmap_in_rect(ctx, (check_if_favourite_route(routes[cell_index->row].Route) ? bitmap_option_tick : bitmap_option_box), GRect(2, (bounds.size.h-OPTION_TICK_SIZE)/2, OPTION_TICK_SIZE, OPTION_TICK_SIZE));
    graphics_draw_text(ctx, routes[cell_index->row].Route, font_24_bold, GRect(20, -2, 30, bounds.size.h-2), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, routes[cell_index->row].Destination, font_24, GRect(20+30+4, 3, bounds.size.w-20-30-4, bounds.size.h-2), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  }
}
static void menu_nearest_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (KEY_LOCATION==last_message && num_nearest>1) {
    int offset=0;
    while (nearest[cell_index->row].Name[offset++]!=' '); // remove "123m " from the platform name
    next_message_callback=&fetch_arrivals;
    add_platform(nearest[cell_index->row].Number,&nearest[cell_index->row].Name[offset], nearest[cell_index->row].Road); 
    window_stack_remove(window_menu, false /* Animated */);
    window_stack_push(window_menu, false /* Animated */);
    //menu_layer_reload_data(menu_layer);
    window_stack_remove(window_menu_nearest, false /* Animated */);
    platform = num_platforms-1;
    window_stack_push(window_list, true /* Animated */);
  } else if (num_routes) {
    char rte[MAX_ROUTE_LENGTH+3];
    snprintf(rte,MAX_ROUTE_LENGTH+3,",%s,",routes[cell_index->row].Route);
    char *start = check_if_favourite_route(routes[cell_index->row].Route);
    if (start!=NULL) { // remove it from the list
      char *offset=start + strlen(rte)-1;
      do *start++=*offset; while (*offset++);
    } else { // add it to the list
      strcat(favourite_routes_list,routes[cell_index->row].Route);
      strcat(favourite_routes_list,",");
    }
    persist_write_string(STORAGE_KEY_FAVOURITE_ROUTES_LIST, favourite_routes_list);
    save_settings_to_phone();
    menu_layer_reload_data(menu_layer);
  }
}
// *****************************************************************************************************
// WINDOWS
// *****************************************************************************************************

// This initializes the menu upon window load
static void window_menu_load(Window *window) {

  // Now we prepare to initialize the menu layer
  // We need the bounds to specify the menu layer's viewport size
  // In this case, it'll be the same as the window's
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);

  // Create the menu layer
  menu_layer = menu_layer_create(bounds);

  // Set all the callbacks for the menu layer
  menu_layer_set_callbacks(menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = menu_get_num_sections_callback,
    .get_num_rows = menu_get_num_rows_callback,
    .get_header_height = menu_get_header_height_callback,
    .get_cell_height = menu_get_cell_height_callback,
    .draw_header = menu_draw_header_callback,
    .draw_row = menu_draw_row_callback,
    .select_click = menu_select_callback,
    .select_long_click = menu_select_long_callback
  });

  // Bind the menu layer's click config provider to the window for interactivity
  menu_layer_set_click_config_onto_window(menu_layer, window);

  // Add it to the window for display
  layer_add_child(window_layer, menu_layer_get_layer(menu_layer));
}

static void window_menu_unload(Window *window) {
  // Destroy the menu layer
  menu_layer_destroy(menu_layer);
}

static void draw_help(GContext *ctx, char *help, uint x, uint y, uint w) {
  GFont font_gothic_14 = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GSize size = graphics_text_layout_get_content_size(help, font_gothic_14 , GRect(x,y,w,200), GTextOverflowModeFill, GTextAlignmentLeft);
  uint h = size.h;
  
  y -= h/2;
  
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(x-6,y-4,w+8,h+8), 3, GCornersAll); 
  
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(x-4,y-2,w+4,h+4), 3, GCornersAll); 

  graphics_context_set_text_color(ctx, GColorWhite); 
  graphics_draw_text(ctx, help, font_gothic_14, GRect(x,y-2,w,h), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

static void arrivals_layer_update_callback(Layer *layer, GContext *ctx) { // screen size = 144 x 168 px
  GFont font_gothic_14 = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GRect bounds = layer_get_frame(layer);
  time_t my_time=time(NULL);
  char *fmt = clock_is_24h_style() ? "%R" : "%l:%M";
  uint eta_width = show_time ? 36 :  16;
  single_arrival_visible_index = -1;
    
  graphics_context_set_text_color(ctx, GColorWhite);
  char title[50];
  if (fetching) {
    snprintf(title,49,"Updating... (x%d)",retries);
    if (retries<2) title[11]=0;
  }
  
  graphics_draw_text(ctx, (fetching?title:platforms[platform].Name), fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GRect(0, 0, bounds.size.w-PLATFORM_WIDTH, PLATFORM_HEIGHT), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, platforms[platform].Number, font_gothic_14, GRect(bounds.size.w-PLATFORM_WIDTH, 0, PLATFORM_WIDTH, PLATFORM_HEIGHT), GTextOverflowModeFill, GTextAlignmentRight, NULL);
  
  GFont font_bold = fonts_get_system_font(num_arrivals_visible<5 ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD);
  GFont font_reg = fonts_get_system_font(num_arrivals_visible<5 ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_18);

  for (int a=0, v=-1, y=PLATFORM_HEIGHT; a<num_arrivals; a++) {

    if (arrivals[a].Visible) {
      if (++v==arrival_selected) {
        arrival_selected_index=a;
        graphics_context_set_fill_color(ctx, GColorWhite);
        uint height = (num_arrivals_visible==1?bounds.size.h:(num_arrivals_visible<5?ARRIVAL_HEIGHT*2:ARRIVAL_HEIGHT));
        graphics_fill_rect(ctx, GRect(0,y+4,bounds.size.w,height), 0, GCornerNone);
        graphics_context_set_text_color(ctx, GColorBlack); 
        scroll_layer_set_content_offset(scroll_layer,GPoint(0,140-(y+height+ARRIVAL_HEIGHT*4)), true /*animated*/); // keep the highlighted row two rows from the bottom of the window.
      } else {
        graphics_context_set_text_color(ctx, GColorWhite);
      }
      if (arrivals[a].Eta==0) {
        title[0]='\0';
      } else if (show_time) {
        time_t eta = my_time + 60*arrivals[a].Eta;
        strftime(title,50,fmt,localtime(&eta));
      } else {
        snprintf(title,50,"%d%s",arrivals[a].Eta,(num_arrivals_visible<5 || trip_selected)?" m":"");
      }
      if (num_arrivals_visible==1) { // window is y+140px high
        single_arrival_visible_index=a;
        graphics_draw_text(ctx, arrivals[a].Route, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD), GRect(0, y, bounds.size.w, 50), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
        graphics_draw_text(ctx, arrivals[a].Destination, fonts_get_system_font(FONT_KEY_GOTHIC_28), GRect(0, y+40, bounds.size.w, 60), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
        graphics_draw_text(ctx, title, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD), GRect(0, y+50+44, bounds.size.w, 50), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
        if (*arrivals[a].Route) {
          if (arrivals[a].Eta>2 || (trip_selected==early_warning.Trip && early_warning.Mins>0)) {
            graphics_draw_bitmap_in_rect(ctx, bitmap_bell, GRect(bounds.size.w-12,y+50+44+20-5*(trip_selected==early_warning.Trip && early_warning.Mins>0),12,14)); // 12x14px
          }
          if (trip_selected==early_warning.Trip && early_warning.Mins>0) {
            snprintf(title,50,"%d",early_warning.Mins);
            graphics_draw_text(ctx, title, font_gothic_14, GRect(bounds.size.w-12, y+50+44+20+10,12,20), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
          }
        }
      } else if (num_arrivals_visible<5) {
        graphics_draw_text(ctx, arrivals[a].Route, font_bold, GRect(0, y-4, bounds.size.w/2, ARRIVAL_HEIGHT), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
        graphics_draw_text(ctx, arrivals[a].Destination, font_reg, GRect(24, y-4+ARRIVAL_HEIGHT+3, bounds.size.w-24, ARRIVAL_HEIGHT-3), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
        graphics_draw_text(ctx, title, font_bold, GRect(bounds.size.w/2, y-4, bounds.size.w/2, ARRIVAL_HEIGHT), GTextOverflowModeFill, GTextAlignmentRight, NULL);
        y+=ARRIVAL_HEIGHT*2;
      } else {
        graphics_draw_text(ctx, arrivals[a].Route, font_bold, GRect(0, y, ROUTE_WIDTH, ARRIVAL_HEIGHT), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
        graphics_draw_text(ctx, arrivals[a].Destination, font_reg, GRect(ROUTE_WIDTH, y, bounds.size.w-eta_width-ROUTE_WIDTH, ARRIVAL_HEIGHT), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
        graphics_draw_text(ctx, title, font_bold, GRect(bounds.size.w-eta_width, y, eta_width, ARRIVAL_HEIGHT), GTextOverflowModeFill, GTextAlignmentRight, NULL);
        y+=ARRIVAL_HEIGHT;
      }
    }
  }
  // DRAW HELP
  if (num_arrivals_visible>1) { // list view
    if (!(help_flags&HELP_FLAGS_ETA)) { // intentional bit wise operator
      draw_help(ctx, "Long press SELECT to toggle arrival time / ETA", 50,82,86);
    } else if (!(help_flags&HELP_FLAGS_DOWN)) { // intentional bit wise operator
      draw_help(ctx, "Press DOWN to scroll down", 50,128,86);
    } else if (!(help_flags&HELP_FLAGS_FULLSCREEN)) { // intentional bit wise operator
      if (arrival_selected!=-1) draw_help(ctx, "Press SELECT to view fullscreen", 50,82,86);
    }
  } else if (arrivals[0].Eta>0) { // fullscreen (but not on "fetching data..." message)
    if (!(help_flags&HELP_FLAGS_ETA)) { // intentional bit wise operator
      draw_help(ctx, "Long press SELECT to toggle arrival time / ETA", 50,82,86);
    } else if (!(help_flags&HELP_FLAGS_ALARM)) { // intentional bit wise operator
      draw_help(ctx, "Press DOWN to set alarm (if ETA is greater than 2m)", 36,127,98);
    }
  }
}

static void scroll_layer_select_click_handler() {
  if (help_flags&HELP_FLAGS_DOWN) help_flags |= HELP_FLAGS_FULLSCREEN;
  if (arrival_selected>-1 && arrivals[arrival_selected_index].Eta) {
    trip_selected=arrivals[arrival_selected_index].Trip;
    arrival_selected=-1;
  }
  redraw_arrivals();
}
static void scroll_layer_select_long_click_handler() {
  if (arrivals[0].Eta>0) {
    show_time=!show_time;
    help_flags |= HELP_FLAGS_ETA;
    redraw_arrivals();
  }
}
static void scroll_layer_back_click_handler() {
  if (trip_selected && ( (favourite_routes_show && num_arrivals_favourites>1) || num_arrivals>1) ) {
    trip_selected=0;
    redraw_arrivals();
  } else {
    window_stack_pop(true);
  }
}
static void scroll_layer_general_click_handler(int delta) {
  arrival_selected+=delta;
  if (arrival_selected<-1 || num_arrivals_visible==1 || trip_selected) arrival_selected=-1;
  if (arrival_selected>=num_arrivals_visible) arrival_selected=num_arrivals_visible-1;
  redraw_arrivals();
}
static void scroll_layer_up_click_handler() { scroll_layer_general_click_handler(-1); }
static void scroll_layer_down_click_handler() { 
  if (num_arrivals_visible==1 || trip_selected) {
    if (help_flags&HELP_FLAGS_ETA) help_flags |= HELP_FLAGS_ALARM;
    uint mins=2;
    if (!trip_selected) trip_selected = arrivals[single_arrival_visible_index].Trip;
    if (trip_selected==early_warning.Trip) {
      mins = (early_warning.Mins+2) % 32; 
      if (mins>=arrivals[single_arrival_visible_index].Eta) mins=0;
    }
    set_early_warning(platform,trip_selected,mins,arrivals[single_arrival_visible_index].Eta);
    redraw_arrivals();
  } else {
    if (help_flags&HELP_FLAGS_ETA) help_flags |= HELP_FLAGS_DOWN;
    scroll_layer_general_click_handler(1);
  }
}

static void scroll_layer_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, (ClickHandler) scroll_layer_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, (ClickHandler) scroll_layer_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, (ClickHandler) scroll_layer_select_click_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, (ClickHandler) scroll_layer_back_click_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, NULL, (ClickHandler) scroll_layer_select_long_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, scroll_layer_up_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, scroll_layer_down_click_handler);
}

// This initializes the list upon window load
static void window_list_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);

#ifdef PBL_SDK_3
  s_status_bar = status_bar_layer_create();
  layer_add_child(window_layer, status_bar_layer_get_layer(s_status_bar));
  
  bounds.origin.y += STATUS_BAR_LAYER_HEIGHT;
  bounds.size.h -= STATUS_BAR_LAYER_HEIGHT;
#endif  
  
  scroll_layer = scroll_layer_create(bounds);
  scroll_layer_set_click_config_onto_window(scroll_layer, window);
  layer_add_child(window_layer, scroll_layer_get_layer(scroll_layer));
  
  arrivals_layer = layer_create(bounds);
  layer_set_update_proc(arrivals_layer, arrivals_layer_update_callback);
  scroll_layer_set_callbacks(scroll_layer, (ScrollLayerCallbacks){
    .click_config_provider = scroll_layer_click_config_provider
  });
  scroll_layer_add_child(scroll_layer, arrivals_layer);
  bitmap_bell=gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BELL);
  
  // set default content for window
  num_arrivals=1;
  num_arrivals_visible=1;
  num_arrivals_favourites=0;
  arrival_selected=-1;
  arrivals[0].Route[0]=0;
  arrivals[0].Eta=0;
  arrivals[0].Trip=trip_selected;
  strncpy(arrivals[0].Destination, "Fetching data...", MAX_DESTINATION_LENGTH);
}

static void window_list_unload(Window *window) {
  layer_destroy(arrivals_layer);
  scroll_layer_destroy(scroll_layer);
  app_timer_cancel(timer);
  platform = -1;
  trip_selected=0;
  if (help_flags!=old_help_flags) {
    persist_write_int(STORAGE_KEY_HELP_FLAGS, old_help_flags=help_flags);
    save_settings_to_phone();
  }
  
  gbitmap_destroy(bitmap_bell);
#ifdef PBL_SDK_3
  status_bar_layer_destroy(s_status_bar);
#endif  
}

// This initializes the menu upon window load
static void window_menu_nearest_load(Window *window) {

  // Now we prepare to initialize the menu layer
  // We need the bounds to specify the menu layer's viewport size
  // In this case, it'll be the same as the window's
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);

  // Create the menu layer
  menu_layer_nearest = menu_layer_create(bounds);

  // Set all the callbacks for the menu layer
  menu_layer_set_callbacks(menu_layer_nearest, NULL, (MenuLayerCallbacks){
    .get_num_sections = menu_nearest_get_num_sections_callback,
    .get_num_rows = menu_nearest_get_num_rows_callback,
    .get_header_height = menu_get_header_height_callback,
    .get_cell_height = menu_nearest_get_cell_height_callback,
    .draw_header = menu_nearest_draw_header_callback,
    .draw_row = menu_nearest_draw_row_callback,
    .select_click = menu_nearest_select_callback,
  });

  // Bind the menu layer's click config provider to the window for interactivity
  menu_layer_set_click_config_onto_window(menu_layer_nearest, window);

  // Add it to the window for display
  layer_add_child(window_layer, menu_layer_get_layer(menu_layer_nearest));
}
  
static void window_menu_nearest_unload(Window *window) {
  // Destroy the menu layer
  menu_layer_destroy(menu_layer_nearest);
  if (routes) free(routes);
  routes=NULL;
  num_routes=0;
}

static void action_bar_increment(int inc) {
  if (number_selected<NUM_DIGITS) {
    number_selector[number_selected] = '0'+ ((number_selector[number_selected]-'0' + inc) % 10);
    strcpy(nearest[0].Name,"");
    strcpy(nearest[0].Number,"");
    strcpy(nearest[0].Road,"");
  } else {
    number_selected=0;
    valid_selector=false;
    strcpy(nearest[0].Name,"");
    strcpy(nearest[0].Number,"");
    strcpy(nearest[0].Road,"");
    action_bar_layer_set_icon(action_bar_layer, BUTTON_ID_UP, bitmap_up);
    action_bar_layer_set_icon(action_bar_layer, BUTTON_ID_DOWN, bitmap_down);
    action_bar_layer_set_icon(action_bar_layer, BUTTON_ID_SELECT, bitmap_right);
  }
  layer_mark_dirty(number_layer);
}

static void action_bar_up_click_handler() {
  action_bar_increment(1);
}

static void action_bar_down_click_handler() {
  action_bar_increment(9);
}
static void action_bar_select_click_handler() {
  if (number_selected<NUM_DIGITS || !valid_selector) {
    number_selected=(number_selected+1) % (NUM_DIGITS+1);
    valid_selector=false;
    if (number_selected==NUM_DIGITS) {
      check_platform(number_selector);
      strcpy(nearest[0].Road,"checking...");
      strcpy(nearest[0].Name,"");
      strcpy(nearest[0].Number,"");
      action_bar_layer_clear_icon(action_bar_layer, BUTTON_ID_UP);
      action_bar_layer_clear_icon(action_bar_layer, BUTTON_ID_DOWN);
    }
  } else { // the platform is valid and user selected to add it
    strcat(nearest[0].Name," - ");
    strcat(nearest[0].Name, nearest[0].Number); // this is actually the bearing
    
    next_message_callback=&fetch_arrivals;
    add_platform(number_selector, nearest[0].Road, nearest[0].Name);
    menu_layer_reload_data(menu_layer);
    window_stack_remove(window_number, false /* Animated */);
    platform = num_platforms-1;
    window_stack_push(window_list, true /* Animated */); 
  }
  layer_mark_dirty(number_layer);
}

static void action_bar_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, (ClickHandler) action_bar_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, (ClickHandler) action_bar_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, (ClickHandler) action_bar_select_click_handler);
}

static void number_layer_update_callback(Layer *layer, GContext *ctx) { // screen size = 144 x 168 px
  GRect bounds = layer_get_frame(layer);  
  GFont font_numbers = fonts_get_system_font(/*FONT_KEY_GOTHIC_28*/ FONT_KEY_BITHAM_34_MEDIUM_NUMBERS);
  GFont font_gothic_18_bold = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont font_gothic_18 = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  GRect grect_add = GRect(bounds.origin.x+30, 64, bounds.size.w-2*30, 26);
  uint number_offset = (bounds.size.w - 2*NUMBER_ORIGIN) / NUM_DIGITS;
  graphics_fill_rect(ctx, 
                         (number_selected<NUM_DIGITS ? GRect(NUMBER_ORIGIN+number_selected*number_offset,20, number_offset, 44) : grect_add)
                          , 1, GCornersAll);
  
  char number[2] = "0";
  for (int a=0, x=NUMBER_ORIGIN; a<NUM_DIGITS; a++, x+=number_offset) {
    *number = number_selector[a];
    graphics_context_set_text_color(ctx, (a==number_selected?GColorWhite:GColorBlack));
    graphics_draw_text(ctx, number, font_numbers, GRect(x-5,20, number_offset+10, 44), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
  grect_add.origin.y-=4;
  graphics_context_set_text_color(ctx, (NUM_DIGITS==number_selected?GColorWhite:GColorBlack));
  graphics_draw_text(ctx, (valid_selector ? "add ?":"check"), fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), grect_add, GTextOverflowModeFill, GTextAlignmentCenter, NULL); 
  
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, nearest[0].Name,   font_gothic_18_bold,  GRect(1,   90,  bounds.size.w-21, 20), GTextOverflowModeFill, GTextAlignmentLeft, NULL); 
  graphics_draw_text(ctx, nearest[0].Number, font_gothic_18_bold,  GRect(bounds.size.w-21, 90,   20, 20), GTextOverflowModeFill, GTextAlignmentRight, NULL); 
  graphics_draw_text(ctx, nearest[0].Road,   font_gothic_18,       GRect(bounds.origin.x+1,   110, bounds.size.w, 40), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

// This initializes the number upon window load
static void window_number_load(Window *window) {

  // Now we prepare to initialize the menu layer
  // We need the bounds to specify the menu layer's viewport size
  // In this case, it'll be the same as the window's
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);
  
  APP_LOG(APP_LOG_LEVEL_INFO, "Loading resources...");
  bitmap_up=gbitmap_create_with_resource(RESOURCE_ID_IMAGE_UP);
  bitmap_down=gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DOWN);
  bitmap_right=gbitmap_create_with_resource(RESOURCE_ID_IMAGE_RIGHT);
  bitmap_tick=gbitmap_create_with_resource(RESOURCE_ID_IMAGE_TICK);

  APP_LOG(APP_LOG_LEVEL_INFO, "Initialising action bar...");
  // Initialize the action bar:
  action_bar_layer = action_bar_layer_create();
  APP_LOG(APP_LOG_LEVEL_INFO, "1...");
  action_bar_layer_add_to_window(action_bar_layer, window);
  APP_LOG(APP_LOG_LEVEL_INFO, "2...");
  action_bar_layer_set_click_config_provider(action_bar_layer, action_bar_click_config_provider);
  APP_LOG(APP_LOG_LEVEL_INFO, "3...");
  action_bar_layer_set_icon(action_bar_layer, BUTTON_ID_UP, bitmap_up);
  action_bar_layer_set_icon(action_bar_layer, BUTTON_ID_DOWN, bitmap_down);
  action_bar_layer_set_icon(action_bar_layer, BUTTON_ID_SELECT, bitmap_right);
  APP_LOG(APP_LOG_LEVEL_INFO, "Initialising action bar animations...");
#if PBL_SDK_3
  action_bar_layer_set_icon_press_animation(action_bar_layer, BUTTON_ID_UP, ActionBarLayerIconPressAnimationMoveUp);
  action_bar_layer_set_icon_press_animation(action_bar_layer, BUTTON_ID_DOWN, ActionBarLayerIconPressAnimationMoveDown);
  action_bar_layer_set_icon_press_animation(action_bar_layer, BUTTON_ID_SELECT, ActionBarLayerIconPressAnimationMoveRight);
#endif
  
  // Initialise the general graphics layer
  bounds.size.w -= ACTION_BAR_WIDTH;
  number_layer = layer_create(bounds);
  layer_set_update_proc(number_layer, number_layer_update_callback);
  layer_add_child(window_layer, number_layer);
  
  // set default contents for window
  strcpy(nearest[0].Name,"");
  strcpy(nearest[0].Road,"");
  strcpy(nearest[0].Number,"");
  number_selected=0;
  valid_selector=false;
  check_platform("");
}
  
static void window_number_unload(Window *window) {
  // Destroy the menu layer
  layer_destroy(number_layer);
  action_bar_layer_destroy(action_bar_layer);
  gbitmap_destroy(bitmap_up);
  gbitmap_destroy(bitmap_down);
  gbitmap_destroy(bitmap_right);
  gbitmap_destroy(bitmap_tick);
}

static void distance_layer_update_callback(Layer *layer, GContext *ctx) { // screen size = 144 x 168 px
  GRect bounds = layer_get_frame(layer);
  
  GFont font_roboto_condensed_21 = fonts_get_system_font(FONT_KEY_ROBOTO_CONDENSED_21);
  GFont font_gothic_18 = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  GRect gr_road = GRect(3, 20, bounds.size.w-2*3-17, 24);
  GRect gr_heading = GRect(3+bounds.size.w-2*3-20, 20, 20, 24);
  GRect gr_name = GRect(3, 44, bounds.size.w-2*3, 20);
  GRect gr_alarm = GRect(60-7, 125, 60+7-9, 21);
  GRect gr_invert;
  char text[10];
  
  if (distance_selection==0) {
    gr_invert=GRect(gr_road.origin.x-1, gr_road.origin.y+5, gr_name.size.w+2, gr_name.origin.y+gr_name.size.h-gr_road.origin.y);
  } else if (distance_selection==1) {
    gr_invert = GRect(64-7, 126, 58+7-9, 24);
  }
  graphics_fill_rect(ctx, gr_invert, 2, GCornersAll);
  
  char road_only[MAX_ROAD_LENGTH+1];
  strncpy(road_only,platforms[distance_alarm.Platform_index].Road,MAX_ROAD_LENGTH);
  char *heading=strstr(road_only," -");
  if (heading) {
    *heading='\0';
    heading+=3;
  }
  
  graphics_context_set_text_color(ctx, distance_selection==0?GColorWhite:GColorBlack);
  graphics_draw_text(ctx, road_only, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),  gr_road, GTextOverflowModeFill, GTextAlignmentLeft, NULL); 
  if (heading) graphics_draw_text(ctx, heading, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),  gr_heading, GTextOverflowModeFill, GTextAlignmentRight, NULL); 
  graphics_draw_text(ctx, platforms[distance_alarm.Platform_index].Name, font_gothic_18,  gr_name, GTextOverflowModeFill, GTextAlignmentLeft, NULL); 
  
  graphics_context_set_text_color(ctx, distance_selection==1?GColorWhite:GColorBlack);
  if (distance_alarm.km) snprintf(text,10,"%d km",distance_alarm.km); else snprintf(text,10,"off");
  graphics_draw_text(ctx, text, font_roboto_condensed_21, gr_alarm, GTextOverflowModeFill, distance_alarm.km?GTextAlignmentRight:GTextAlignmentCenter, NULL); 
  
  if (distance_refreshing) graphics_draw_bitmap_in_rect(ctx, bitmap_refresh, GRect(29-14-3,63+49/2-13/2+5,14,13));
  
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, "Distance to stop", font_gothic_18,  GRect(0, 0, bounds.size.w, 21), GTextOverflowModeFill, GTextAlignmentCenter, NULL); 
  snprintf(text,10,"%d",platforms_distances[distance_alarm.Platform_index]);
  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49), GRect(29-5, 63, 60, 49), GTextOverflowModeFill, GTextAlignmentRight, NULL); 
  graphics_draw_text(ctx, "km", font_roboto_condensed_21, GRect(90-5, 90, 34, 23), GTextOverflowModeFill, GTextAlignmentLeft, NULL); 
  graphics_draw_text(ctx, "Alarm:", font_roboto_condensed_21, GRect(0, 125, 56, 24), GTextOverflowModeFill, GTextAlignmentRight, NULL); 
  
  // DRAW HELP
  if (!(help_flags&HELP_FLAGS_DISTANCE)) { // intentional bit wise operator
    draw_help(ctx, "Note: The alarm will stay active even if you close this window or app.", 8, 82, 112);
  }
}

static void distance_action_bar_updown_click_handler(int sign) {
  if (distance_selection==2) { // ignore
    return;
  } else if (distance_selection==0) { // change platform
    sign = (sign>0) ? 1 : num_platforms-1;
    distance_alarm.Platform_index = (distance_alarm.Platform_index+sign) % num_platforms;
    set_distance_warning();
  } else if (distance_selection==1) { // change alarm
    if (platforms_distances[distance_alarm.Platform_index]>0) {
      sign = (sign>0) ? 1 : platforms_distances[distance_alarm.Platform_index]-1;
      distance_alarm.km = (distance_alarm.km+sign) % (platforms_distances[distance_alarm.Platform_index]);
    } else distance_alarm.km=0;
    set_distance_warning();
  }
  layer_mark_dirty(distance_layer);
}
  
static void distance_action_bar_up_click_handler()   { distance_action_bar_updown_click_handler(1); }
static void distance_action_bar_down_click_handler() { distance_action_bar_updown_click_handler(-1); }

static void distance_action_bar_select_click_handler() {
  help_flags |= HELP_FLAGS_DISTANCE;
  
  distance_selection=(distance_selection+1)%3;
  action_bar_layer_set_icon(distance_action_bar_layer, BUTTON_ID_UP, (distance_selection==2) ? NULL : bitmap_up);
  action_bar_layer_set_icon(distance_action_bar_layer, BUTTON_ID_DOWN, (distance_selection==2) ? NULL : bitmap_down);
  layer_mark_dirty(distance_layer);
}

static void distance_action_bar_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, (ClickHandler) distance_action_bar_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, (ClickHandler) distance_action_bar_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, (ClickHandler) distance_action_bar_select_click_handler);
}

// This initializes the distance upon window load
static void window_distance_load(Window *window) {

  // Now we prepare to initialize the menu layer
  // We need the bounds to specify the menu layer's viewport size
  // In this case, it'll be the same as the window's  
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);

#ifdef PBL_SDK_3
  s_status_bar = status_bar_layer_create();
  layer_add_child(window_layer, status_bar_layer_get_layer(s_status_bar));
  
  bounds.origin.y += STATUS_BAR_LAYER_HEIGHT;
  bounds.size.h -= STATUS_BAR_LAYER_HEIGHT;
#endif  
  
  bitmap_up=gbitmap_create_with_resource(RESOURCE_ID_IMAGE_UP);
  bitmap_down=gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DOWN);
  bitmap_right=gbitmap_create_with_resource(RESOURCE_ID_IMAGE_RIGHT);
  bitmap_refresh=gbitmap_create_with_resource(RESOURCE_ID_IMAGE_REFRESH);

  // Initialize the action bar:
  distance_action_bar_layer = action_bar_layer_create();
  action_bar_layer_add_to_window(distance_action_bar_layer, window);
  action_bar_layer_set_click_config_provider(distance_action_bar_layer, distance_action_bar_click_config_provider);
  action_bar_layer_set_icon(distance_action_bar_layer, BUTTON_ID_SELECT, bitmap_right);
#if PBL_SDK_3
  action_bar_layer_set_icon_press_animation(distance_action_bar_layer, BUTTON_ID_UP, ActionBarLayerIconPressAnimationMoveUp);
  action_bar_layer_set_icon_press_animation(distance_action_bar_layer, BUTTON_ID_DOWN, ActionBarLayerIconPressAnimationMoveDown);
  action_bar_layer_set_icon_press_animation(distance_action_bar_layer, BUTTON_ID_SELECT, ActionBarLayerIconPressAnimationMoveRight);
#endif
  
  // Initialise the general graphics layer
  bounds.size.w -= ACTION_BAR_WIDTH;
  distance_layer = layer_create(bounds);
  layer_set_update_proc(distance_layer, distance_layer_update_callback);
  layer_add_child(window_layer, distance_layer);
  
  if (distance_alarm.Platform_index>=num_platforms) distance_alarm.Platform_index=num_platforms-1;
  for (uint a=0; a<num_platforms; a++) platforms_distances[a]=0;
  distance_selection=2;
  fetch_favourite_distances();
}

static void window_distance_unload(Window *window) {
  // Destroy the menu layer
  layer_destroy(distance_layer);
  action_bar_layer_destroy(distance_action_bar_layer);
  gbitmap_destroy(bitmap_up);
  gbitmap_destroy(bitmap_down);
  gbitmap_destroy(bitmap_right);
  gbitmap_destroy(bitmap_refresh);
  app_timer_cancel(timer);
  if (help_flags!=old_help_flags) {
    persist_write_int(STORAGE_KEY_HELP_FLAGS, old_help_flags=help_flags);
    save_settings_to_phone();
  }
#ifdef PBL_SDK_3
  status_bar_layer_destroy(s_status_bar);
#endif
}

// *****************************************************************************************************
// MAIN
// *****************************************************************************************************
static void wakeup_handler(WakeupId id, int32_t reason) {
  //APP_LOG(APP_LOG_LEVEL_INFO, "woken up! id:%d, reason:%d", (int)id,(int)reason);
  wakeup_cancel_all();
  if (STORAGE_KEY_EARLY_WARNING==reason) {
    if (window_stack_contains_window(window_list)) {
      window_stack_remove(window_list, true); // this will also cancel the active timer
    }
    persist_read_data(STORAGE_KEY_EARLY_WARNING, &early_warning, sizeof(early_warning));
    persist_delete(STORAGE_KEY_EARLY_WARNING);
    platform=early_warning.Platform;
    trip_selected=early_warning.Trip;
    window_stack_push(window_list, true /* Animated */);
    fetch_arrivals();
  } else if (STORAGE_KEY_DISTANCE_ALARM==reason) {
    persist_read_data(STORAGE_KEY_DISTANCE_ALARM, &distance_alarm, sizeof(distance_alarm));
    persist_delete(STORAGE_KEY_DISTANCE_ALARM);
    if (!window_stack_contains_window(window_distance)) window_stack_push(window_distance, true /* Animated */);
  }
}
  
static void init(void) {
  window_menu = window_create();
  window_set_window_handlers(window_menu, (WindowHandlers) {
    .load = window_menu_load,
    .unload = window_menu_unload,
  });
  bitmap_option_tick=gbitmap_create_with_resource(RESOURCE_ID_IMAGE_OPTION_TICK);
  bitmap_option_box=gbitmap_create_with_resource(RESOURCE_ID_IMAGE_OPTION_BOX);
  
  window_list = window_create();
  window_set_background_color(window_list, GColorBlack);
  window_set_window_handlers(window_list, (WindowHandlers) {
    .load = window_list_load,
    .unload = window_list_unload,
  });
  
  window_menu_nearest = window_create();
  window_set_window_handlers(window_menu_nearest, (WindowHandlers) {
    .load = window_menu_nearest_load,
    .unload = window_menu_nearest_unload,
  });
  
  window_number = window_create(); 
  window_set_window_handlers(window_number, (WindowHandlers) {
    .load = window_number_load,
    .unload = window_number_unload,
  });
  
  window_distance = window_create(); 
  window_set_window_handlers(window_distance, (WindowHandlers) {
    .load = window_distance_load,
    .unload = window_distance_unload,
  });
  
	// Register AppMessage handlers
  app_message_register_inbox_received(inbox_received_handler); 
  //app_message_register_inbox_dropped(inbox_dropped_handler); 
  app_message_register_outbox_failed(outbox_failed_handler);
  app_message_register_outbox_sent(outbox_sent_handler);
  outbox_size=636; //app_message_outbox_size_maximum();
  app_message_open(outbox_size, outbox_size);
  //APP_LOG(APP_LOG_LEVEL_INFO, "sizeof platforms:%d, arrivals:%d, outbox:%d", (int)sizeof(platforms), (int)sizeof(arrivals), outbox_size);
  
  // Read stored data from watch.
  num_platforms=0;
  uint32_t stored_version = persist_read_int(STORAGE_KEY_VERSION); // defaults to 0 if key is missing
  autoselect=persist_read_bool(STORAGE_KEY_AUTOSELECT); // defaults to false anyway
  favourite_routes_show=persist_read_bool(STORAGE_KEY_FAVOURITE_ROUTES_SHOW); // introduced in storage version 2, defaults to false anyway
  old_help_flags=help_flags=persist_read_int(STORAGE_KEY_HELP_FLAGS);  // introduced in storage version 2, defaults to 0 anyway
  if (persist_exists(STORAGE_KEY_FAVOURITE_ROUTES_LIST)) { // introduced in storage version 2
    persist_read_string(STORAGE_KEY_FAVOURITE_ROUTES_LIST, favourite_routes_list, MAX_FAV_ROUTES_LIST_LENGTH);
  } else {
    snprintf(favourite_routes_list,3,",");
  }
  char plats[(MAX_PL_NUM_LENGTH+1)*MAX_PLATFORMS+6]="auto|";
  while (persist_exists(STORAGE_KEY_PLATFORM+num_platforms) && num_platforms<MAX_PLATFORMS) {
    persist_read_data(STORAGE_KEY_PLATFORM+num_platforms, &platforms[num_platforms], sizeof(platforms[num_platforms]));
    strcat(plats,platforms[num_platforms].Number);
    strcat(plats,";");
    num_platforms++;
  }
  
  wakeup_service_subscribe(wakeup_handler);
  window_stack_push(window_menu, true /* Animated */);
  
  if (launch_reason() == APP_LAUNCH_WAKEUP) {
    // Get details and handle the event
    WakeupId id = 0;
    int32_t reason = 0;
    wakeup_get_launch_event(&id, &reason);
    wakeup_handler(id,reason);
  } else {
    if (persist_exists(STORAGE_KEY_EARLY_WARNING)) { // introduced in storage version 2
      persist_read_data(STORAGE_KEY_EARLY_WARNING, &early_warning, sizeof(early_warning));
    } else {
      early_warning.Trip=0;
    }
    if (persist_exists(STORAGE_KEY_DISTANCE_ALARM)) {
      persist_read_data(STORAGE_KEY_DISTANCE_ALARM, &distance_alarm, sizeof(distance_alarm));
    }
    if (num_platforms && autoselect) fetch_nearest_platforms(plats);
  }
  
  data_exists = (num_platforms>0 || persist_exists(STORAGE_KEY_AUTOSELECT) );
  if (stored_version < CURRENT_STORAGE_VERSION) persist_write_int(STORAGE_KEY_VERSION, CURRENT_STORAGE_VERSION);

  //LOG_HEAP("");
}

static void deinit(void) {
	app_message_deregister_callbacks();
	window_destroy(window_menu);
  window_destroy(window_list);
  window_destroy(window_menu_nearest);
  window_destroy(window_number);
  window_destroy(window_distance);
  gbitmap_destroy(bitmap_option_tick);
  gbitmap_destroy(bitmap_option_box);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}