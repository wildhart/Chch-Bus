#include "pebble.h"

#define MAX_PLATFORMS 8
#define MAX_PL_NUM_LENGTH 6
#define MAX_NAME_LENGTH 32
#define MAX_ROAD_LENGTH 32
struct platform {
  char Number[MAX_PL_NUM_LENGTH+1];
  char Name[MAX_NAME_LENGTH+1];
  char Road[MAX_ROAD_LENGTH+1];
} platforms[MAX_PLATFORMS]; 
int num_platforms = 0;
int platform = -1;

#define MAX_ARRIVALS 10
#define MAX_ROUTE_LENGTH 4
#define MAX_DESTINATION_LENGTH 32
#define MAX_ETA_LENGTH 2
struct arrival {
  char Route[MAX_ROUTE_LENGTH+1];
  char Destination[MAX_DESTINATION_LENGTH+1];
  char Eta[MAX_ETA_LENGTH+1];
} arrivals[MAX_ARRIVALS];
int num_arrivals=0;
int num_arrivals_favourites=0;
int num_arrivals_visible=0;

enum { // main menu structure
  MENU_SECTION_FAVS,
  MENU_SECTION_ADD,
  MENU_SECTION_OPTIONS,
  MENU_SECTION_REMOVE,
  
  NUM_MENU_SECTIONS,
  
  MENU_ADD_NUMBER=MENU_SECTION_ADD*100,
  MENU_ADD_LOCATION,
  NUM_MENU_ITEMS_ADD=2,
  
  MENU_OPTIONS_AUTO=MENU_SECTION_OPTIONS*100,
  MENU_OPTIONS_ROUTES,
  NUM_MENU_ITEMS_OPTIONS=2
};
#define MENU_SECTION_CELL (cell_index->section * 100 + cell_index->row)
#define MENU_HEIGHT_SINGLE 30
#define MENU_HEIGHT_DOUBLE 44
static Window *window_menu;
static MenuLayer *menu_layer;

#define PLATFORM_WIDTH 30
#define PLATFORM_HEIGHT 12
#define ARRIVAL_HEIGHT 16
#define ROUTE_WIDTH 24
#define ETA_WIDTH 16
static Window *window_list;
static ScrollLayer *scroll_layer;
static Layer *arrivals_layer;
int fetching = false;
int retries = 0;

struct nearest {
  char Number[MAX_PL_NUM_LENGTH+1];
  char Name[MAX_NAME_LENGTH+1];
  char Road[MAX_ROAD_LENGTH+1];
} nearest[MAX_PLATFORMS]; 
int num_nearest=0;
struct route {
  char Route[MAX_ROUTE_LENGTH+1];
  char Destination[MAX_DESTINATION_LENGTH+1];
} *routes;
int num_routes=0;
static Window *window_menu_nearest;
static MenuLayer *menu_layer_nearest;

#define NUM_DIGITS 5
#define NUMBER_OFFSET 24
#define NUMBER_ORIGIN 2 // Action bar layer =20px wide
#define valid_selector() (nearest[0].Number[0]!='\0')
char *number_selector = "50529"; // Central station
static Window *window_number;
static Layer *number_layer;
ActionBarLayer *action_bar_layer;
static GBitmap *bitmap_up;
static GBitmap *bitmap_down;
static GBitmap *bitmap_right;
static GBitmap *bitmap_tick;
static GBitmap *bitmap_option_tick;
static GBitmap *bitmap_option_box;
#define OPTION_TICK_SIZE 16
int number_selected = 0;

AppTimer *timer;

// Key values for AppMessage Dictionary
#define KEY_ARRIVALS 0
#define KEY_LOCATION 1
#define KEY_CHECK_PLATFORM 2
#define KEY_JS_READY 3
#define KEY_NEAREST_FAV 4
#define KEY_SAVE_SETTINGS 5
#define KEY_GET_ROUTES 6
bool js_ready = false;
bool js_outbox_waiting = false;
int last_message;
void (*next_message_callback)(void);
  
// Persistent Storage Keys
#define STORAGE_KEY_AUTOSELECT 1  
#define STORAGE_KEY_VERSION 2
#define STORAGE_KEY_FAVOURITE_ROUTES_SHOW 3
#define STORAGE_KEY_FAVOURITE_ROUTES_LIST 4
#define STORAGE_KEY_PLATFORM 100
#define CURRENT_STORAGE_VERSION 2
bool autoselect = false;
bool favourite_routes_show = false;
#define MAX_FAV_ROUTES_LIST_LENGTH 32
char favourite_routes_list[MAX_FAV_ROUTES_LIST_LENGTH+1];
bool data_exists = false;

// *****************************************************************************************************
// MESSAGES
// *****************************************************************************************************

static void redraw_arrivals() {
  // Set correct size of arrivals list and redraw  
  GRect bounds = layer_get_frame(scroll_layer_get_layer(scroll_layer));
  uint height=num_arrivals_visible==1 ? 168-PLATFORM_HEIGHT-16 : (ARRIVAL_HEIGHT+ARRIVAL_HEIGHT*(num_arrivals_visible>4 ? num_arrivals_visible : num_arrivals_visible<<1));
  scroll_layer_set_content_size(scroll_layer, GSize(bounds.size.w, PLATFORM_HEIGHT+height));
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
    
    //APP_LOG(APP_LOG_LEVEL_INFO, "Sending Platform %s.", platforms[platform].Number);
    Tuplet value = TupletCString(last_message=KEY_ARRIVALS, platforms[platform].Number);
    dict_write_tuplet(iter, &value);
   
    send_message_when_phone_ready();
    fetching = true;
    retries++;
    redraw_arrivals();
    
    // set timer for next fetch.
    timer = app_timer_register(20000, fetch_arrivals, NULL);
  }
}

static char* check_if_favourite_route(char *route) {
  char rte[MAX_ROUTE_LENGTH+3];
  snprintf(rte,MAX_ROUTE_LENGTH+3,",%s,",route);
  return strstr(favourite_routes_list,rte); // pointer to substring if true, NULL if false
}

static void process_arrivals(char *source) {
  char bus[3][MAX_NAME_LENGTH+1];
  uint s=0; // source offset
  uint c=0; // column (0=route, 1=destination, 2 = eta)
  num_arrivals=0;
  num_arrivals_favourites=0;
  num_arrivals_visible=0;
  while (source[s] && num_arrivals<MAX_ARRIVALS) {
    uint d=0; // destination offset
    while (source[s] && source[s]!=';' && d<MAX_NAME_LENGTH) {
      bus[c][d++]=source[s++];
    }
    bus[c++][d]=0;
    s++;
    if (c==3) {
      strncpy(arrivals[num_arrivals].Route, bus[0], MAX_ROUTE_LENGTH);
      strncpy(arrivals[num_arrivals].Destination, bus[1], MAX_DESTINATION_LENGTH);
      strncpy(arrivals[num_arrivals].Eta, bus[2], MAX_ETA_LENGTH);
      // APP_LOG(APP_LOG_LEVEL_INFO, "route:%s; dest:%s; eta:%s", arrivals[num_arrivals].Route,arrivals[num_arrivals].Destination,arrivals[num_arrivals].Eta);
      if (check_if_favourite_route(arrivals[num_arrivals].Route)) num_arrivals_favourites++; // there is a favourite, so only show favourites
      num_arrivals++;
      c=0;
    }
  }
  num_arrivals_visible = (favourite_routes_show && num_arrivals_favourites) ? num_arrivals_favourites : num_arrivals;
  fetching=false;
  retries=0;
  redraw_arrivals();
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
  char bus[3][MAX_NAME_LENGTH+1];
  uint s=0; // source offset
  uint c=0; // column (0=number, 1=name, 3=road)
  num_nearest=0;
  while (source[s] && num_arrivals<MAX_ARRIVALS) {
    uint d=0; // destination offset
    while (source[s] && source[s]!=';' && d<MAX_NAME_LENGTH) {
      bus[c][d++]=source[s++];
    }
    bus[c++][d]=0;
    s++;
    if (c==3) {
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
  //APP_LOG(APP_LOG_LEVEL_INFO, "Platform checked: %s", source);
  char bus[3][MAX_NAME_LENGTH+1];
  uint s=0; // source offset
  for (int c=0; c<3; c++) {
    uint d=0; // destination offset
    while (source[s] && source[s]!=';' && d<MAX_NAME_LENGTH) {
      bus[c][d++]=source[s++];
    }
    bus[c][d]=0;
    s++;
  }
  num_nearest=0;
  strncpy(nearest[num_nearest].Name, bus[0], MAX_NAME_LENGTH);
  strncpy(nearest[num_nearest].Road, bus[1], MAX_ROAD_LENGTH);
  strncpy(nearest[num_nearest].Number, bus[2], MAX_PL_NUM_LENGTH);
  
  if (!valid_selector() && number_selected==NUM_DIGITS) number_selected=0;
  action_bar_layer_set_icon(action_bar_layer, BUTTON_ID_UP, (valid_selector() && number_selected==NUM_DIGITS) ? bitmap_right : bitmap_up);
  action_bar_layer_set_icon(action_bar_layer, BUTTON_ID_DOWN, (valid_selector() && number_selected==NUM_DIGITS)? bitmap_right : bitmap_down);
  action_bar_layer_set_icon(action_bar_layer, BUTTON_ID_SELECT, (valid_selector() && number_selected==NUM_DIGITS) ? bitmap_tick : bitmap_right);
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
  uint s=0; // source offset
  uint c=0; // column (0=number, 1=name, 3=road)
  num_routes=0;
  
  // first count number of routes to allocate enough memory.
  while (source[s]) {
    if (source[s++]==';') num_routes++; // this will counnt twice the number of routes
  }
  
  struct route *r=malloc( (num_routes>>1) * sizeof(struct route)); // >> 1 halves the number of routes.
  routes = r;
  
  num_routes=0;
  s=0;
  while (source[s]) {
    uint d=0; // destination offset
    while (source[s] && source[s]!=';' && d<MAX_NAME_LENGTH) {
      bus[c][d++]=source[s++];
    }
    bus[c++][d]=0;
    s++;
    if (c==columns) {
      strncpy(r[num_routes].Route, bus[0], MAX_ROUTE_LENGTH);
      strncpy(r[num_routes].Destination, bus[1], MAX_DESTINATION_LENGTH);
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
  uint s=0; // source offset
  uint c=0; // column (0=number, 1=name, 3=road)
//  uint32_t version=0;
  num_platforms=0;
  char plats[(MAX_PL_NUM_LENGTH+1)*MAX_PLATFORMS]="";
  
  while (source[s]) {
    uint d=0; // destination offset
    while (source[s] && source[s]!=';' && source[s]!='|' && d<MAX_NAME_LENGTH) {
      bus[c][d++]=source[s++];
    }
    bus[c++][d]=0;
    if (source[s]=='|') {
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
    s++;
  }

  if (num_platforms && autoselect) fetch_nearest_platforms(plats);
  menu_layer_set_selected_index(menu_layer, MenuIndex(0,0), MenuRowAlignTop, false);
  menu_layer_reload_data(menu_layer);
}

static void save_settings_to_phone() {
  DictionaryIterator *iter;
  char size=app_message_outbox_size_maximum();
  char message[size];
  int len=0;
  
  app_message_outbox_begin(&iter);
  len+=snprintf(message+len,size-len,"%d;%d|",(int)STORAGE_KEY_VERSION, (int)CURRENT_STORAGE_VERSION);
  len+=snprintf(message+len,size-len,"%d;%d|",(int)STORAGE_KEY_AUTOSELECT, (int)autoselect);
  len+=snprintf(message+len,size-len,"%d;%d|",(int)STORAGE_KEY_FAVOURITE_ROUTES_SHOW, (int)favourite_routes_show);
  len+=snprintf(message+len,size-len,"%d;%s|",(int)STORAGE_KEY_FAVOURITE_ROUTES_LIST, favourite_routes_list);
  for (int a=0; a<num_platforms; a++) {
    len+=snprintf(message+len,size-len,"%d;%s;%s;%s|",STORAGE_KEY_PLATFORM+a,platforms[a].Number,platforms[a].Name,platforms[a].Road);
  }
  
  dict_write_cstring(iter, KEY_SAVE_SETTINGS, message);
 
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

char *translate_error(AppMessageResult result) {
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
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped, reason:%s",translate_error(reason));
  if (next_message_callback) {
    next_message_callback();
    next_message_callback=NULL;
  }
}

// Called when PebbleKitJS does not acknowledge receipt of a message
static void outbox_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed, reason:%s",translate_error(reason));
  if (next_message_callback) {
    next_message_callback();
    next_message_callback=NULL;
  }
}
// Called when PebbleKitJS does not acknowledge receipt of a message
static void outbox_sent_handler(DictionaryIterator *sent, void *context) {
  //APP_LOG(APP_LOG_LEVEL_INFO, "Message received: %d", next_message_callback ? 1 : 0);
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
    case MENU_SECTION_ADD: return NUM_MENU_ITEMS_ADD;
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

// This is the menu item draw callback where you specify what each item should look like
static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  // Determine which section we're going to draw in
  switch (cell_index->section) {
    case MENU_SECTION_FAVS:
    case MENU_SECTION_REMOVE:
      menu_cell_basic_draw(ctx, cell_layer, platforms[cell_index->row].Road, platforms[cell_index->row].Name, NULL);
      break;

    default:
      switch (MENU_SECTION_CELL) {
        case MENU_ADD_NUMBER: menu_cell_basic_draw(ctx, cell_layer, "By Number", NULL, NULL); break;
        case MENU_ADD_LOCATION: menu_cell_basic_draw(ctx, cell_layer, "By Location", NULL, NULL); break;
        case MENU_OPTIONS_AUTO: menu_cell_basic_draw(ctx, cell_layer, "Auto Select", "Nearest Favourite", autoselect ? bitmap_option_tick : bitmap_option_box); break;
        case MENU_OPTIONS_ROUTES: menu_cell_basic_draw(ctx, cell_layer, "Fav routes",num_platforms?"Long click to edit":"Add fav stop first", favourite_routes_show ? bitmap_option_tick : bitmap_option_box); break;
      }
  }
}

// Here we capture when a user selects a menu item
static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  
  switch (cell_index->section) {
    case MENU_SECTION_FAVS: // select favourite
      platform = cell_index->row;
      retries = 0;
      window_stack_push(window_list, true /* Animated */);
      fetch_arrivals();
      break;
    case MENU_SECTION_REMOVE: // delete favourite
      for (int a=cell_index->row; a<num_platforms-1; a++) {
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
    for (int a=0; a<num_platforms; a++) {
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
  if (KEY_LOCATION==last_message || num_routes==0) {
    menu_cell_basic_draw(ctx, cell_layer, nearest[cell_index->row].Road, nearest[cell_index->row].Name, NULL);
  } else {
      GFont font_24_bold = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
      GRect bounds = layer_get_frame(cell_layer);
      
      graphics_context_set_text_color(ctx, GColorBlack);
//      graphics_draw_bitmap_in_rect(ctx, (check_if_favourite_route(routes[cell_index->row].Route) ? bitmap_option_tick : bitmap_option_box), GRect(4, (bounds.size.h-OPTION_TICK_SIZE)>>1, OPTION_TICK_SIZE, OPTION_TICK_SIZE));
      graphics_draw_bitmap_in_rect(ctx, (check_if_favourite_route(routes[cell_index->row].Route) ? bitmap_option_tick : bitmap_option_box), GRect(4, (bounds.size.h-OPTION_TICK_SIZE)>>1, OPTION_TICK_SIZE, OPTION_TICK_SIZE));
      graphics_draw_text(ctx, routes[cell_index->row].Route, font_24_bold, GRect(24, -2, 30, bounds.size.h-2), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
      graphics_draw_text(ctx, routes[cell_index->row].Destination, font_24_bold, GRect(24+30+4, -2, bounds.size.w-24-30-4, bounds.size.h-2), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  }
}
static void menu_nearest_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (KEY_LOCATION==last_message && num_nearest>1) {
    int offset=0;
    while (nearest[cell_index->row].Name[offset++]!=' '); // remove "123m " from the platform name
    next_message_callback=&fetch_arrivals;
    add_platform(nearest[cell_index->row].Number,&nearest[cell_index->row].Name[offset], nearest[cell_index->row].Road); 
    window_stack_remove(window_menu, false /* Animated */);
    window_stack_push(window_menu, false);
    window_stack_remove(window_menu_nearest, false /* Animated */);
    platform = num_platforms-1;
    window_stack_push(window_list, true /* Animated */);
  } else if (KEY_GET_ROUTES==last_message && num_routes) {
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
void window_menu_load(Window *window) {

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

void window_menu_unload(Window *window) {
  // Destroy the menu layer
  menu_layer_destroy(menu_layer);
}

void arrivals_layer_update_callback(Layer *layer, GContext *ctx) { // screen size = 144 x 168 px
  GRect bounds = layer_get_frame(layer);
  bool favs_only = favourite_routes_show && (num_arrivals_favourites>0);
    
  graphics_context_set_text_color(ctx, GColorWhite);
  char title[50];
  if (fetching) {
    snprintf(title,49,"Updating... (x%d)",retries);
    if (retries<2) title[11]=0;
  }
  
  graphics_draw_text(ctx, (fetching?title:platforms[platform].Name), fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GRect(0, 0, bounds.size.w-PLATFORM_WIDTH, PLATFORM_HEIGHT), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, platforms[platform].Number, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(bounds.size.w-PLATFORM_WIDTH, 0, PLATFORM_WIDTH, PLATFORM_HEIGHT), GTextOverflowModeFill, GTextAlignmentRight, NULL);
  
  GFont font_bold = fonts_get_system_font(num_arrivals_visible<5 ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD);
  GFont font_reg = fonts_get_system_font(num_arrivals_visible<5 ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_18);
  for (int a=0, y=PLATFORM_HEIGHT; a<num_arrivals; a++) {
    if (!favs_only || check_if_favourite_route(arrivals[a].Route)) {
      if (num_arrivals_visible==1) { // window is y+140px high
        snprintf(title,50,"%s%s",arrivals[a].Eta,arrivals[a].Eta[0]?" m":"");
        graphics_draw_text(ctx, arrivals[a].Route, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD), GRect(0, y, bounds.size.w, 50), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
        graphics_draw_text(ctx, arrivals[a].Destination, fonts_get_system_font(FONT_KEY_GOTHIC_28), GRect(0, y+40, bounds.size.w, 60), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
        graphics_draw_text(ctx, title, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD), GRect(0, y+50+44, bounds.size.w, 50), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
      } else if (num_arrivals_visible<5) {
        snprintf(title,50,"%s%s",arrivals[a].Eta,arrivals[a].Eta[0]?" m":"");
        graphics_draw_text(ctx, arrivals[a].Route, font_bold, GRect(0, y, bounds.size.w >> 1, ARRIVAL_HEIGHT), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
        graphics_draw_text(ctx, arrivals[a].Destination, font_reg, GRect(24, y+ARRIVAL_HEIGHT+3, bounds.size.w-24, ARRIVAL_HEIGHT-3), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
        graphics_draw_text(ctx, title, font_bold, GRect(bounds.size.w >> 1, y, bounds.size.w >> 1, ARRIVAL_HEIGHT), GTextOverflowModeFill, GTextAlignmentRight, NULL);
        y+=ARRIVAL_HEIGHT << 1;
      } else {
        graphics_draw_text(ctx, arrivals[a].Route, font_bold, GRect(0, y, ROUTE_WIDTH, ARRIVAL_HEIGHT), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
        graphics_draw_text(ctx, arrivals[a].Destination, font_reg, GRect(ROUTE_WIDTH, y, bounds.size.w-ETA_WIDTH-ROUTE_WIDTH, ARRIVAL_HEIGHT), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
        graphics_draw_text(ctx, arrivals[a].Eta, font_bold, GRect(bounds.size.w-ETA_WIDTH, y, ETA_WIDTH, ARRIVAL_HEIGHT), GTextOverflowModeFill, GTextAlignmentRight, NULL);
        y+=ARRIVAL_HEIGHT;
      }
    }
  }
}
  
// This initializes the list upon window load
void window_list_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);
  
  scroll_layer = scroll_layer_create(bounds);
  scroll_layer_set_click_config_onto_window(scroll_layer, window);
  layer_add_child(window_layer, scroll_layer_get_layer(scroll_layer));
  
  arrivals_layer = layer_create(bounds);
  layer_set_update_proc(arrivals_layer, arrivals_layer_update_callback);
  scroll_layer_add_child(scroll_layer, arrivals_layer);
  
  // set default content for window
  num_arrivals=1;
  num_arrivals_visible=1;
  num_arrivals_favourites=0;
  arrivals[0].Route[0]=0;
  strncpy(arrivals[0].Destination, "Fetching data...", MAX_DESTINATION_LENGTH);
  arrivals[0].Eta[0]=0;
}

void window_list_unload(Window *window) {
  // Destroy the layer
  platform = -1;
  app_timer_cancel(timer);
  layer_destroy(arrivals_layer);
  scroll_layer_destroy(scroll_layer);
}

// This initializes the menu upon window load
void window_menu_nearest_load(Window *window) {

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
  
void window_menu_nearest_unload(Window *window) {
  // Destroy the menu layer
  menu_layer_destroy(menu_layer_nearest);
  if (routes) free(routes);
  routes=NULL;
  num_routes=0;
}

void action_bar_increment(int inc) {
  if (number_selected<NUM_DIGITS) {
    number_selector[number_selected] = '0'+ ((number_selector[number_selected]-'0' + inc) % 10);
    strcpy(nearest[0].Name,"");
    strcpy(nearest[0].Number,"");
    strcpy(nearest[0].Road,"");
  } else {
    number_selected=0;
    strcpy(nearest[0].Name,"");
    strcpy(nearest[0].Number,"");
    strcpy(nearest[0].Road,"");
    action_bar_layer_set_icon(action_bar_layer, BUTTON_ID_UP, bitmap_up);
    action_bar_layer_set_icon(action_bar_layer, BUTTON_ID_DOWN, bitmap_down);
    action_bar_layer_set_icon(action_bar_layer, BUTTON_ID_SELECT, bitmap_right);
  }
  layer_mark_dirty(number_layer);
}

void action_bar_up_click_handler() {
  action_bar_increment(1);
}

void action_bar_down_click_handler() {
  action_bar_increment(9);
}
void action_bar_select_click_handler() {
  if (number_selected<NUM_DIGITS || !valid_selector()) {
    number_selected=(number_selected+1) % (NUM_DIGITS+1);
    if (number_selected==NUM_DIGITS) {
      check_platform(number_selector);
      strcpy(nearest[0].Road,"checking...");
      strcpy(nearest[0].Name,"");
      strcpy(nearest[0].Number,"");
      action_bar_layer_clear_icon(action_bar_layer, BUTTON_ID_UP);
      action_bar_layer_clear_icon(action_bar_layer, BUTTON_ID_DOWN);
    }
  } else { // the platform is valid and use selected to add it
    strcat(nearest[0].Name," - ");
    strcat(nearest[0].Name, nearest[0].Number); // this is actually the bearing
    
    next_message_callback=&fetch_arrivals;
    add_platform(number_selector, nearest[0].Road, nearest[0].Name); 
    window_stack_remove(window_menu, false /* Animated */);
    window_stack_push(window_menu, false);
    window_stack_remove(window_number, false /* Animated */);
    platform = num_platforms-1;
    window_stack_push(window_list, true /* Animated */);
  }
  layer_mark_dirty(number_layer);
}

void action_bar_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, (ClickHandler) action_bar_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, (ClickHandler) action_bar_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, (ClickHandler) action_bar_select_click_handler);
}

void number_layer_update_callback(Layer *layer, GContext *ctx) { // screen size = 144 x 168 px
  //GRect bounds = layer_get_frame(layer);
  GFont font_numbers = fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS);
  GFont font_18_bold = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont font_18 = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  
  GRect grect_add = GRect(27, 64, 70, 26);
  graphics_fill_rect(ctx, 
                         (number_selected<NUM_DIGITS ? GRect(NUMBER_ORIGIN+number_selected*NUMBER_OFFSET,20, NUMBER_OFFSET, 44) : grect_add)
                          , 1, GCornersAll);
  
  for (int a=0, x=NUMBER_ORIGIN; a<NUM_DIGITS; a++, x+=NUMBER_OFFSET) {
    char *number = "1";
    number[0] = number_selector[a];
    graphics_context_set_text_color(ctx, (a==number_selected?GColorWhite:GColorBlack));
    graphics_draw_text(ctx, number, font_numbers, GRect(x,20, NUMBER_OFFSET, 44), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
  grect_add.origin.y-=4;
  graphics_context_set_text_color(ctx, (NUM_DIGITS==number_selected?GColorWhite:GColorBlack));
  graphics_draw_text(ctx, (nearest[0].Number[0]=='\0' ? "check":"add ?"), fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), grect_add, GTextOverflowModeFill, GTextAlignmentCenter, NULL); 
  
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, nearest[0].Name,   font_18_bold,  GRect(0,   90,  103, 20), GTextOverflowModeFill, GTextAlignmentLeft, NULL); 
  graphics_draw_text(ctx, nearest[0].Number, font_18_bold,  GRect(103, 90,   21, 20), GTextOverflowModeFill, GTextAlignmentRight, NULL); 
  graphics_draw_text(ctx, nearest[0].Road,   font_18,       GRect(0,   110, 124, 40), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

// This initializes the number upon window load
void window_number_load(Window *window) {

  // Now we prepare to initialize the menu layer
  // We need the bounds to specify the menu layer's viewport size
  // In this case, it'll be the same as the window's
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);

  // Initialize the action bar:
  action_bar_layer = action_bar_layer_create();
  action_bar_layer_add_to_window(action_bar_layer, window);
  action_bar_layer_set_click_config_provider(action_bar_layer, action_bar_click_config_provider);
  action_bar_layer_set_icon(action_bar_layer, BUTTON_ID_UP, bitmap_up);
  action_bar_layer_set_icon(action_bar_layer, BUTTON_ID_DOWN, bitmap_down);
  action_bar_layer_set_icon(action_bar_layer, BUTTON_ID_SELECT, bitmap_right);
  
  // Initialise the general graphics layer
  number_layer = layer_create(bounds);
  layer_set_update_proc(number_layer, number_layer_update_callback);
  layer_add_child(window_layer, number_layer);
  
  // set default contents for window
  strcpy(nearest[0].Name,"");
  strcpy(nearest[0].Road,"");
  strcpy(nearest[0].Number,"");
  number_selected=0;
  check_platform("");
}
  
void window_number_unload(Window *window) {
  // Destroy the menu layer
  //action_bar_layer_destroy(action_bar_layer);
  layer_destroy(number_layer);
  action_bar_layer_destroy(action_bar_layer);
}

// *****************************************************************************************************
// MAIN
// *****************************************************************************************************

void init(void) {
  window_menu = window_create();
  // Setup the window handlers
  window_set_window_handlers(window_menu, (WindowHandlers) {
    .load = window_menu_load,
    .unload = window_menu_unload,
  });
  
  window_list = window_create();
  window_set_background_color(window_list, GColorBlack);
  // Setup the window handlers
  window_set_window_handlers(window_list, (WindowHandlers) {
    .load = window_list_load,
    .unload = window_list_unload,
  });
  
  window_menu_nearest = window_create();
  // Setup the window handlers
  window_set_window_handlers(window_menu_nearest, (WindowHandlers) {
    .load = window_menu_nearest_load,
    .unload = window_menu_nearest_unload,
  });
  
  window_number = window_create(); 
  // Setup the window handlers
  window_set_window_handlers(window_number, (WindowHandlers) {
    .load = window_number_load,
    .unload = window_number_unload,
  });
  bitmap_up=gbitmap_create_with_resource(RESOURCE_ID_IMAGE_UP);
  bitmap_down=gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DOWN);
  bitmap_right=gbitmap_create_with_resource(RESOURCE_ID_IMAGE_RIGHT);
  bitmap_tick=gbitmap_create_with_resource(RESOURCE_ID_IMAGE_TICK);
  bitmap_option_tick=gbitmap_create_with_resource(RESOURCE_ID_IMAGE_OPTION_TICK);
  bitmap_option_box=gbitmap_create_with_resource(RESOURCE_ID_IMAGE_OPTION_BOX);
  
	// Register AppMessage handlers
	app_message_register_inbox_received(inbox_received_handler); 
	app_message_register_inbox_dropped(inbox_dropped_handler); 
	app_message_register_outbox_failed(outbox_failed_handler);
  app_message_register_outbox_sent(outbox_sent_handler);
	app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
  
  // Read stored data from watch.
  num_platforms=0;
  uint32_t stored_version = persist_read_int(STORAGE_KEY_VERSION); // defaults to 0 if key is missing.
  autoselect=persist_read_bool(STORAGE_KEY_AUTOSELECT);
  favourite_routes_show=persist_read_bool(STORAGE_KEY_FAVOURITE_ROUTES_SHOW); // introduced in storage version 2
  if (persist_exists(STORAGE_KEY_FAVOURITE_ROUTES_LIST)) { // introduced in storage version 2
    persist_read_string(STORAGE_KEY_FAVOURITE_ROUTES_LIST, favourite_routes_list, MAX_FAV_ROUTES_LIST_LENGTH);
  } else {
    snprintf(favourite_routes_list,3,",");
  }
  char plats[(MAX_PL_NUM_LENGTH+1)*MAX_PLATFORMS]="";
  while (persist_exists(STORAGE_KEY_PLATFORM+num_platforms) && num_platforms<MAX_PLATFORMS) {
    persist_read_data(STORAGE_KEY_PLATFORM+num_platforms, &platforms[num_platforms], sizeof(platforms[num_platforms]));
    strcat(plats,platforms[num_platforms].Number);
    strcat(plats,";");
    num_platforms++;
  }    
  data_exists = (num_platforms>0 || persist_exists(STORAGE_KEY_AUTOSELECT) );
  if (num_platforms && autoselect) fetch_nearest_platforms(plats);
  if (stored_version < CURRENT_STORAGE_VERSION) persist_write_int(STORAGE_KEY_VERSION, CURRENT_STORAGE_VERSION);

  window_stack_push(window_menu, true /* Animated */);
  
  // APP_LOG(APP_LOG_LEVEL_INFO, "sizeof arrivals:%d, outbox:%d", (int)sizeof(arrivals), (int)app_message_outbox_size_maximum());
}

void deinit(void) {
	app_message_deregister_callbacks();
	window_destroy(window_menu);
  window_destroy(window_list);
  window_destroy(window_menu_nearest);
  window_destroy(window_number);
  gbitmap_destroy(bitmap_up);
  gbitmap_destroy(bitmap_down);
  gbitmap_destroy(bitmap_right);
  gbitmap_destroy(bitmap_tick);
  gbitmap_destroy(bitmap_option_tick);
  gbitmap_destroy(bitmap_option_box);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
