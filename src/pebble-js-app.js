var platformNo = null;        // number of current platform
var platformsOBJ=null;        // object full of platform data
var get_platforms_list = [];  // list of platforms for which routes need to be fetched
var get_platforms_routes;     // will store list of routes for favourite platforms
var buffer_size;

// variables used for calculating distance between 2 coordinates
var deg2rad = 0.017453292519943295; // === Math.PI / 180
var cos = Math.cos;
var asin = Math.asin;
var sqrt = Math.sqrt;
var diam = 12742; // Diameter of the earth in km (2 * 6371)
var lat,lon;

var nearest_limit=10;
var location_reason=null;

Pebble.addEventListener('ready', function(e) {
  var settings=localStorage.getItem("settings");
	console.log("Sending message: KEY_JS_READY " + settings);
  Pebble.sendAppMessage({'KEY_JS_READY': (settings) ? settings : ""});
});

// Called when incoming message from the Pebble is received
Pebble.addEventListener("appmessage",	function(e) {
	console.log("Received message: " + JSON.stringify(e.payload));
  for (var p in e.payload) {
    if (p=="KEY_ARRIVALS") {
      var data=e.payload.KEY_ARRIVALS.split(";");
      buffer_size=data[0];
      platformNo=data[1];
      get_platforms_list=[];
      update();
    } else if (p=="KEY_LOCATION") {
      if ((typeof e.payload.KEY_LOCATION)=='number') { // get locations of all platforms and return nearest N
        location_reason='all';
        nearest_limit = e.payload.KEY_LOCATION;
      } else {  // find distance of favourite platforms
        location_reason=e.payload.KEY_LOCATION; // this is the list of favourite platforms
      }
      if (!platformsOBJ) {
        get_platforms('location'); // will call get_location() when done.
      } else {
        get_location();
      }
    } else if (p=="KEY_CHECK_PLATFORM") {
      if (!platformsOBJ) {
        get_platforms(e.payload.KEY_CHECK_PLATFORM); 
      } else {
        check_platform(e.payload.KEY_CHECK_PLATFORM);
      }
    } else if (p=="KEY_SAVE_SETTINGS") {
      localStorage.setItem('settings',e.payload.KEY_SAVE_SETTINGS);
    } else if (p=="KEY_GET_ROUTES") {
      get_platforms_list=e.payload.KEY_GET_ROUTES.split(';'); // there is a trailing ; so last item in array is blank
      platformNo = get_platforms_list.shift();
      get_platforms_routes=[];
      update();
    } else {
      console.log("Received unknown message: " + JSON.stringify(e.payload));
    }
  }
});

function check_platform(platform) {
  console.log(platform);
  if (!platform) return; // this was used just to cache the platforms
  var message=platformsOBJ[platform] ? (platformsOBJ[platform].RoadName?platformsOBJ[platform].RoadName:'')+";"+(platformsOBJ[platform].Name?platformsOBJ[platform].Name:'')+';'+(platformsOBJ[platform].BearingToRoad?bearing_to_text(platformsOBJ[platform].BearingToRoad):'')+";"
                                : ";Invalid platform;;";
	console.log("Sending message: KEY_CHECK_PLATFORM " + message);
  Pebble.sendAppMessage({'KEY_CHECK_PLATFORM': message});
}

// ******************************* GET BUS ARRIVALS

function update() {
  if (!platformNo) return;  
  var req = new XMLHttpRequest();
  req.open('GET', 'http://rtt.metroinfo.org.nz/rtt/public/utility/file.aspx?ContentType=SQLXML&Name=JPRoutePositionET&PlatformNo='+platformNo, true);
  req.onload = function(e) {
    if (req.readyState == 4 && req.status == 200) {
      if(req.status == 200) {
        //console.log('AJAX Resonse: ' + req.responseText);
        var busses=XMLtoarray(req.responseText);
        var message='';
        //platformName=data.match(/<Platform .* Name="(.*?)"/i)[1];
        if (get_platforms_list.length) {
          for (var a in busses) if (get_platforms_routes[busses[a].route]===undefined) get_platforms_routes[busses[a].route] = busses[a].name.substr(0,14);
          platformNo=get_platforms_list.shift();
          if (platformNo) {
            update();
          } else {
            message="";
            for (a in get_platforms_routes) {
              message+=a+";"+get_platforms_routes[a]+";";
            }
            console.log("Sending message: KEY_GET_ROUTES "+message);
            Pebble.sendAppMessage({'KEY_GET_ROUTES':message});
            platformNo="";
          }
        } else {
          message=objToString(busses);
            console.log("Sending message: KEY_ARRIVALS "+message.substr(0,buffer_size));
          Pebble.sendAppMessage({'KEY_ARRIVALS':message.substr(0,buffer_size)});
        }
      } else { console.log('Error'); }
    } else { console.log('Error 2'); }
  };
  req.send(null);
}

function XMLtoarray(data) {
  // return bus arrival data, sorted by ETA
  var busses=[];
  var routes=data.match(/<Route ([\S\s]*?)<\/Route>/gi);
  if (!routes) return [{route:'', destination: 'No buses due  in the next', eta:data.match(/MaxArrivalScope="(\d*)/)[1]}];
  for (var r=0; r<routes.length; r++) {
    var route=routes[r].match(/RouteNo="(\S*)"/i)[1];
    var name=routes[r].match(/Name="(.*?)"/i)[1];
    var destinations=routes[r].match(/<Destination ([\S\s]*?)<\/Destination>/gi);
    for (var d=0; d<destinations.length; d++) {
      var dest_name=destinations[d].match(/Name="(.*?)"/i)[1];
      var trips=destinations[d].match(/<Trip (.*?)\/>/gi);
      for (var e=0; e<trips.length; e++) {
        var eta=trips[e].match(/ETA="(\d+)/)[1];
        var trip=trips[e].match(/TripNo="(\d+)/)[1];
        busses.push({route:route, name:name, destination:dest_name.replace(/&amp;/g, "&").substr(0,32), eta:eta, trip:trip});
      }
    }
  }
  busses.sort(function(a, b){return a.eta-b.eta; });
  return busses;
}

function objToString (obj) {
  var str = '';
  for (var p in obj) {
     if (obj.hasOwnProperty(p)) {
          str += obj[p].route+";"+obj[p].destination+";"+obj[p].eta+';'+obj[p].trip+';';
      }
  }
  return str;
}

// ******************************* GET LOCATION

function locationSuccess(pos) {
  lat=pos.coords.latitude;
  lon=pos.coords.longitude;
  if (lat>0) {
    lat = -43.533121;  lon = 172.626011; // Chch hospital
  }
  // lat=-43.536471;  lon=172.586957; Platform 51556 which has no bearing or RoadName
  
  lat *= deg2rad; // convert to radians
  lon *= deg2rad;
  
  if (location_reason=='all') {
    calculate_distance_all();
  } else {
    calculate_distance_favourites();
  }
}

function locationError(err) {
  console.log('location error (' + err.code + '): ' + err.message);
}

function get_location() {
  var locationOptions = {
    enableHighAccuracy: true, 
    maximumAge: 10000, // in milliseconds
    timeout: 40000
  };
  // Make an asynchronous request
  navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
}

function calculate_distance_all() { 
  var nearest_max=10000;
  var i=0;
  var plat;
  var dist;
  var nearest=[];
  
  for (var PlatformNo in platformsOBJ) {
    plat=platformsOBJ[PlatformNo];
    dist=distance(lat,lon,plat.Lat*1,plat.Long*1);
    if (dist<nearest_max) {
      for (i=0; i<nearest.length; i++) {
        if (dist < nearest[i].Dist) {
          nearest.splice(i,0,{PlatformNo:PlatformNo, Dist:dist});
          if (nearest.length > nearest_limit) {
            nearest.pop();
          }
          break;
        }
      }
      if (i==nearest.length && i<nearest_limit) {
        nearest[i]={PlatformNo:PlatformNo, Dist:dist};
      }
      nearest_max = nearest[nearest.length-1].Dist;
    }
  }
  
  var items="";
  for (i=0; i<nearest.length; i++) {
    plat=platformsOBJ[nearest[i].PlatformNo];
    dist=nearest[i].Dist;
    dist = (dist<1.0) ? (dist*1000).toFixed(0)+'m ' : dist.toFixed(2)+'k ';
    items+=(nearest[i].PlatformNo?nearest[i].PlatformNo:'')+";"+dist+plat.Name.substr(0,24)+";"+(plat.RoadName?plat.RoadName:'') + ' - ' + (plat.BearingToRoad?bearing_to_text(plat.BearingToRoad):'')+";";
    //console.log((nearest[i].PlatformNo?nearest[i].PlatformNo:'')+";"+dist+plat.Name.substr(0,24)+";"+(plat.RoadName?plat.RoadName:'') + ' - ' + (plat.BearingToRoad?bearing_to_text(plat.BearingToRoad):'')+";");
  }
  //console.log(items.length);
  console.log("Sending message: KEY_LOCATION "+items);
  Pebble.sendAppMessage({'KEY_LOCATION':items});
}

function calculate_distance_favourites() {
  var nearest=10000;
  var plat;
  var dist;
  var index;
  var data=location_reason.split("|");
  var reason=data[0];
  var platforms = data[1].split(";");
  var items="";
  
  for (var i=0; i<platforms.length-1; i++) {
    plat=platformsOBJ[platforms[i]];
    dist=distance(lat,lon,plat.Lat*1,plat.Long);
    items+=Math.round(dist)+";";
    if (dist<nearest) {
      nearest=dist;
      index=i;
    }
  }
  if (reason=='auto') { // only send index of nearest favourite
    console.log("Sending message: KEY_NEAREST_FAV "+index);
    Pebble.sendAppMessage({'KEY_NEAREST_FAV':index});
  } else if (reason=='alarm') { // send distance of all favourites, for the distance alarm window
    console.log("Sending message: KEY_DISTANCE_FAV "+items);
    Pebble.sendAppMessage({'KEY_DISTANCE_FAV':items});
  }
}

function bearing_to_text(bearing) {
  var points=['S','SW','W','NW','N','NE','E','SE', 'S']; // Bearing is TO road, so 0deg means stop looks North so road, so stop is on SOUTH side.
  var i=Math.floor((bearing*1.0+22.5)/45);
  //console.log(bearing,i,points[i]);
  return points[i];
}

function distance(lat1, lon1, lat2, lon2) { // all parameters should already be in radians
  var a = (
     (1 - cos(lat2 - lat1)) + 
     (1 - cos(lon2 - lon1)) * cos(lat1) * cos(lat2)
  ) / 2;
  return diam * asin(sqrt(a));
}

// ******************************* FETCH PLATFORMS

function get_platforms(reason) {
  var date=new Date();
  var old_date=localStorage.getItem("platforms_date");
  if (old_date && (date-old_date) < 7 * 24 * 60 * 60 * 1000 ) { // if data is more than 7 days old (in milliseconds)
    parse_platforms(localStorage.getItem('platforms'), reason);
    return;
  }
  var req = new XMLHttpRequest();
  req.open('GET', 'http://rtt.metroinfo.org.nz/rtt/public/utility/file.aspx?ContentType=SQLXML&Name=JPPlatform', true);
  req.onreadystatechange  = function(e) {
    if (req.readyState == 4) {
      if(req.status == 200) {
        //console.log('AJAX Resonse: ' + req.responseText);
        localStorage.setItem('platforms',req.responseText);
        localStorage.setItem('platforms_date',date.valueOf()); // milliseconds
        parse_platforms(req.responseText,reason);
      } else {
        console.log('Error');
        if (old_date) parse_platforms(localStorage.getItem('platforms'),reason);
      }
    }
  };
  req.send(null);
}

function parse_platforms(platforms, reason) {
  var reg_platforms=new RegExp("<Platform ([\\s\\S]*?)<\/Platform>",'gi');
  var match=null;
  platformsOBJ=[];
  while (match=reg_platforms.exec(platforms)) { // intended asignment to variable match
    var reg_props=new RegExp('([a-zA-Z]*)="(.*?)"','g');
    var obj={};
    var match2=null;
    while(match2=reg_props.exec(match[1])) { // intended asignment to variable match
      obj[match2[1]]=match2[2];
    }
    obj.Lat  *= deg2rad; // convert to radians
    obj.Long *= deg2rad;
    platformsOBJ[obj.PlatformNo]=obj;
  }
  if (reason=='location') get_location(); else check_platform(reason);
}