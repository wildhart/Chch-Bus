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

#define NUM_MENU_SECTIONS 4
#define NUM_MENU_ITEMS_ADD 2
#define NUM_MENU_ITEMS_OPTIONS 1
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
static Window *window_menu_nearest;
static MenuLayer *menu_layer_nearest;
int num_nearest=0;

#define NUM_DIGITS 5
#define NUMBER_OFFSET 24
#define NUMBER_ORIGIN 2 // Action bar layer =20px wide
#define valid_selector() (nearest[0].Number[0]!='\0')
char *number_selector = "50529";
static Window *window_number;
static Layer *number_layer;
ActionBarLayer *action_bar_layer;
static GBitmap *bitmap_up;
static GBitmap *bitmap_down;
static GBitmap *bitmap_right;
static GBitmap *bitmap_tick;
int number_selected = 0;

AppTimer *timer;
bool autoselect;

// Key values for AppMessage Dictionary
#define KEY_ARRIVALS 0
#define KEY_LOCATION 1
#define KEY_CHECK_PLATFORM 2
#define KEY_JS_READY 3
#define KEY_NEAREST_FAV 4
bool js_ready = false;
bool js_outbox_waiting = false;
  
// Persistent Storage Keys
#define STORAGE_KEY_AUTOSELECT 1  
#define STORAGE_KEY_PLATFORM 100

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
  }
}

// *****************************************************************************************************
// MESSAGES
// *****************************************************************************************************

static void redraw_arrivals() {
  // Set correct size of arrivals list and redraw  
  GRect bounds = layer_get_frame(scroll_layer_get_layer(scroll_layer));
  scroll_layer_set_content_size(scroll_layer, GSize(bounds.size.w, PLATFORM_HEIGHT+ARRIVAL_HEIGHT*(num_arrivals+1)));
  layer_set_frame(arrivals_layer, GRect(0,0, bounds.size.w, PLATFORM_HEIGHT+ARRIVAL_HEIGHT*(num_arrivals+1)));
  layer_mark_dirty(scroll_layer_get_layer(scroll_layer));
}

// Write message to buffer & send
static void fetch_arrivals() {
  if (platform>=0) {
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    
    //APP_LOG(APP_LOG_LEVEL_INFO, "Sending Platform %s.", platforms[platform].Number);
    Tuplet value = TupletCString(KEY_ARRIVALS, platforms[platform].Number);
    dict_write_tuplet(iter, &value);
   
    if (js_ready) app_message_outbox_send(); else js_outbox_waiting=true;
    fetching = true;
    retries++;
    redraw_arrivals();
    
    // set timer for next fetch.
    timer = app_timer_register(20000, fetch_arrivals, NULL);
  }
}

static void process_arrivals(char *source) {
  char bus[3][MAX_NAME_LENGTH+1];
  uint s=0; // source offset
  uint c=0; // column (0=route, 1=destination, 2 = eta)
  num_arrivals=0;
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
      num_arrivals++;
      c=0;
    }
  }
  fetching=false;
  retries=0;
  redraw_arrivals();
}


static void fetch_nearest_platforms(char *favourites) {
  #define get_nearest_favourite favourites[0]
    
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  Tuplet value = get_nearest_favourite ? TupletCString(KEY_LOCATION, favourites) : TupletInteger(KEY_LOCATION, MAX_PLATFORMS);
  dict_write_tuplet(iter, &value);
 
  if (js_ready) app_message_outbox_send(); else js_outbox_waiting=true;
  
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
  
  Tuplet value = TupletCString(KEY_CHECK_PLATFORM, platform);
  dict_write_tuplet(iter, &value);
 
  if (js_ready) app_message_outbox_send(); else js_outbox_waiting=true;
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
      case KEY_JS_READY:
        js_ready=true;
        if (js_outbox_waiting) {
          app_message_outbox_send();
          js_outbox_waiting=false;
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
        APP_LOG(APP_LOG_LEVEL_ERROR, "Key %d not recognized!", (int)t->key);
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
}

// Called when PebbleKitJS does not acknowledge receipt of a message
static void outbox_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed, reason:%s",translate_error(reason));
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
    case 0:
    case 2: return num_platforms;
    case 1: return NUM_MENU_ITEMS_ADD;
    case 3: return NUM_MENU_ITEMS_OPTIONS;
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
  if (cell_index->section == 1) {
    return 30;
  }
  return 44;
}

// Here we draw what each header is
static void menu_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  // Determine which section we're working with
  switch (section_index) {
    case 0:  menu_cell_basic_header_draw(ctx, cell_layer, "Favourite stops"); break;
    case 1:  menu_cell_basic_header_draw(ctx, cell_layer, "Add stop"); break;
    case 2:  menu_cell_basic_header_draw(ctx, cell_layer, "Remove stop"); break;
    case 3:  menu_cell_basic_header_draw(ctx, cell_layer, "Options"); break;
  }
}

// This is the menu item draw callback where you specify what each item should look like
static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  // Determine which section we're going to draw in
  switch (cell_index->section) {
    case 0:
    case 2:
      menu_cell_basic_draw(ctx, cell_layer, platforms[cell_index->row].Road, platforms[cell_index->row].Name, NULL);
      break;

    default:
      switch (cell_index->section * 100 + cell_index->row) {
        case 100: menu_cell_basic_draw(ctx, cell_layer, "By Number", NULL, NULL); break;
        case 101: menu_cell_basic_draw(ctx, cell_layer, "By Location", NULL, NULL); break;
        case 300: menu_cell_basic_draw(ctx, cell_layer, autoselect?"Auto Select: Yes":"Auto Select: No", "Nearest Favourite", NULL); break;
      }
  }
}

// Here we capture when a user selects a menu item
static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  
  switch (cell_index->section) {
    case 0: // select favourite
      platform = cell_index->row;
      retries = 0;
      window_stack_push(window_list, true /* Animated */);
      fetch_arrivals();
      break;
    case 1: // Add favourite
      switch (cell_index->row) {
        case 0: // By Number
          window_stack_push(window_number, true /* Animated */);
          break;
        case 1: // By Location
          fetch_nearest_platforms("");
          break;
      }
      break;
    case 2: // delete favourite
      for (int a=cell_index->row; a<num_platforms-1; a++) {
        platforms[a]=platforms[a+1];
        persist_write_data(STORAGE_KEY_PLATFORM+a, &platforms[a], sizeof(platforms[a]));
      }
      num_platforms--;
      persist_delete(STORAGE_KEY_PLATFORM+num_platforms);
      menu_layer_reload_data(menu_layer);
      break;
    case 3: // options
      persist_write_bool(STORAGE_KEY_AUTOSELECT, autoselect=!autoselect);
      menu_layer_reload_data(menu_layer);
      break;
  }
}

static uint16_t menu_nearest_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  return 1;
}
static uint16_t menu_nearest_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return num_nearest;
}
static void menu_nearest_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
   menu_cell_basic_header_draw(ctx, cell_layer, "Nearest stops");
}
static void menu_nearest_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  menu_cell_basic_draw(ctx, cell_layer, nearest[cell_index->row].Road, nearest[cell_index->row].Name, NULL);
}
static void menu_nearest_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (num_nearest>1) {
    int offset=0;
    while (nearest[cell_index->row].Name[offset++]!=' '); // remove "123m " from the platform name
    add_platform(nearest[cell_index->row].Number,&nearest[cell_index->row].Name[offset], nearest[cell_index->row].Road);
    window_stack_remove(window_menu, false /* Animated */);
    window_stack_push(window_menu, false);
    window_stack_remove(window_menu_nearest, false /* Animated */);
    platform = num_platforms-1;
    window_stack_push(window_list, true /* Animated */);
    fetch_arrivals();
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
  
  graphics_context_set_text_color(ctx, GColorWhite);
  char title[50];
  if (fetching) {
    snprintf(title,49,"Updating... (x%d)",retries);
    if (retries<2) title[11]=0;
  }
  graphics_draw_text(ctx, (fetching?title:platforms[platform].Name), fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GRect(0, 0, bounds.size.w-PLATFORM_WIDTH, PLATFORM_HEIGHT), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, platforms[platform].Number, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(bounds.size.w-PLATFORM_WIDTH, 0, PLATFORM_WIDTH, PLATFORM_HEIGHT), GTextOverflowModeFill, GTextAlignmentRight, NULL);
  
  GFont font_18_bold = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont font_18 = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  for (int a=0, y=PLATFORM_HEIGHT; a<num_arrivals; a++, y+=ARRIVAL_HEIGHT) {
      graphics_draw_text(ctx, arrivals[a].Route, font_18_bold, GRect(0, y, ROUTE_WIDTH, ARRIVAL_HEIGHT), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
      graphics_draw_text(ctx, arrivals[a].Destination, font_18, GRect(ROUTE_WIDTH, y, bounds.size.w-ETA_WIDTH-ROUTE_WIDTH, ARRIVAL_HEIGHT), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
      graphics_draw_text(ctx, arrivals[a].Eta, font_18_bold, GRect(bounds.size.w-ETA_WIDTH, y, ETA_WIDTH, ARRIVAL_HEIGHT), GTextOverflowModeFill, GTextAlignmentRight, NULL);
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
    .get_cell_height = menu_get_cell_height_callback,
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
    add_platform(number_selector, nearest[0].Road, nearest[0].Name);
    window_stack_remove(window_menu, false /* Animated */);
    window_stack_push(window_menu, false);
    window_stack_remove(window_number, false /* Animated */);
    platform = num_platforms-1;
    window_stack_push(window_list, true /* Animated */);
    fetch_arrivals();
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
  
	// Register AppMessage handlers
	app_message_register_inbox_received(inbox_received_handler); 
	app_message_register_inbox_dropped(inbox_dropped_handler); 
	app_message_register_outbox_failed(outbox_failed_handler);
	app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
  
  num_platforms=0;
  char plats[(MAX_PL_NUM_LENGTH+1)*MAX_PLATFORMS]="";
  while (persist_exists(STORAGE_KEY_PLATFORM+num_platforms) && num_platforms<MAX_PLATFORMS) {
    persist_read_data(STORAGE_KEY_PLATFORM+num_platforms, &platforms[num_platforms], sizeof(platforms[num_platforms]));
    strcat(plats,platforms[num_platforms].Number);
    strcat(plats,";");
    num_platforms++;
  }
  autoselect=persist_read_bool(STORAGE_KEY_AUTOSELECT);
  if (!num_platforms) {
    //add_platform("45166","Birchs Rd near Glenwood Dr","Birchs Rd - W");
    //add_platform("14704","Christchurch Hospital","Tuam St - S");
  } else if (autoselect) {
    fetch_nearest_platforms(plats);
  }
  
  window_stack_push(window_menu, true /* Animated */);
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
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
