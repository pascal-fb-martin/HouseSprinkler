// Copyrigth (C) Pascal Martin, 2013.
//
// NAME
//
//   sprinklerlib - a library of JavaScript web functions.
//
// SYNOPSYS
//
//   This module provides a set of common JavaScript functions
//   used in multiple web pages.
//
// DESCRIPTION
//
//   sprinklerInfo();
//
//      This function populates the page's title and all HTML items marked
//      with known CSS class names with information from the controler:
//
//      hostname        the controler's host name.
//      activezone      the currently active zone name, or else "Idle".
//      activeprogram   the currently active program, or else "Idle".
//      raindelay       'DISABLED', 'NONE' or remaining duration.
//      weatherupdated  the time of the last weather update.
//      temperature     the temperature in the last weather update.
//      humidity        the humidity level in the last weather update.
//      rain            the rain level in the last weather update.
//      rainsensor      the state of the rain sensor (ON, OFF), as computed
//                      from the last weather update.
//      adjustment      the weather adjustment level, as computed from
//                      the last weather update.
//
//   sprinklerOnOff();
//
//      This function toggles the sprinkler scheduler between on and off.
//
//   sprinklerConfig(callback);
//
//      This function retrieves the user configuration.
//
//   sprinklerConfigZones(callback);
//
//      This function retrieves the watering zones configuration.
//
//   sprinklerSaveConfig(config);
//
//      This function sends the user configuration to the server. The
//      configuration sent will replace the current one.
//
//   sprinklerStatus(callback);
//
//      This function retrieves the current status.
//
//   sprinklerHardwareInfo(callback);
//
//      This function retrieves the current hardware information.
//
//   sprinklerZoneOn(index, duration);
//
//      This function requests the controler to start the specified zone
//      for the specified duration.
//
//   sprinklerZoneOff();
//
//      This function requests the controler to stop all zones (and programs).
//
//   sprinklerRefresh();
//
//      This function requests the controler to refresh all information
//      from the outsid world: weather, calendar programs.
//
//   sprinklerHistory(callback);
//
//      This function requests the complete history.
//
//   sprinklerLatestEvent(callback);
//
//      This function requests the ID of the latest event.
//

function sprinklerShowDuration (seconds) {
   var minutes = Math.floor(seconds / 60);
   seconds = Math.floor(seconds % 60);
   if (minutes > 60) {
      var hours = Math.floor(minutes / 60);
      minutes = Math.floor(minutes % 60);
      return ('00'+hours).slice(-2)+':'+('00'+minutes).slice(-2)+':'+('00'+seconds).slice(-2);
   }
   return ('00'+minutes).slice(-2)+':'+('00'+seconds).slice(-2);
}

function sprinklerSetContent (classname, content) {
   var elements = document.getElementsByClassName (classname);
   for (var i = 0; i < elements.length; i++) {
      elements[i].innerHTML = content;
   }
}

function sprinklerApplyUpdate (text) {

   // var type = command.getResponseHeader("Content-Type");
   var response = JSON.parse(text);
   var content;
   var content2;

   sprinklerSetContent ('hostname', response.sprinkler.host);

   if (response.sprinkler.program.active) {
      content2 = response.sprinkler.program.active[0];
   } else {
      content2 = null;
   }
   if (response.sprinkler.control.active) {
      content = 'ZONE '+response.sprinkler.control.active+' ACTIVE';
      if (! content2) content2 = 'MANUAL';
   } else {
      content = 'IDLE';
      if (! content2) content2 = 'IDLE';
   }

   if (response.on == false) {
      content2 = 'OFF';
   }
   sprinklerSetContent ('activezone', content);
   sprinklerSetContent ('activeprogram', content2);

   if (! response.sprinkler.program.rain.enabled) {
      content = 'DISABLED';
   } else if (response.sprinkler.program.rain.delay) {
      var deadline = new Date(response.raintimer).getTime();
      var delta = Math.floor((deadline - new Date().getTime()) / 1000);
      if (delta <= 0) {
         content = 'NONE';
      } else {
         content = sprinklerShowDuration(delta);
      }
   } else {
      content = 'NONE';
   }
   sprinklerSetContent ('raindelay', content);

   if (response.sprinkler.program.index) {
      sprinklerSetContent ('adjustment',
          ''+response.sprinkler.index.value+'%'+' FROM '+response.sprinkler.index.origin);
   } else {
      sprinklerSetContent ('adjustment','NOT AVAILABLE');
   }

   title = document.getElementsByClassName ('hostname');
   var title = 'Sprinkler Controler '+response.host;
   document.getElementsByTagName ('title')[0].innerHTML = title;
}

function sprinklerUpdate () {
   var command = new XMLHttpRequest();
   command.open("GET", "/status");
   command.onreadystatechange = function () {
       if (command.readyState === 4 && command.status === 200) {
           sprinklerApplyUpdate(command.responseText);
       }
   }
   command.send(null);
}

function sprinklerInfo () {
   sprinklerUpdate();
   setInterval (sprinklerUpdate, 1000);
}

function sprinklerConfig (callback) {
   var command = new XMLHttpRequest();
   command.open("GET", "/config");
   command.onreadystatechange = function () {
      if (command.readyState === 4 && command.status === 200) {
         var config = JSON.parse(command.responseText);
         // var type = command.getResponseHeader("Content-Type");
         callback(config);
      }
   }
   command.send(null);
}

function sprinklerConfigZones(callback) {
   var command = new XMLHttpRequest();
   command.open("GET", "/config/zones");
   command.onreadystatechange = function () {
      if (command.readyState === 4 && command.status === 200) {
         var config = JSON.parse(command.responseText);
         // var type = command.getResponseHeader("Content-Type");
         callback(config);
      }
   }
   command.send(null);
}

function sprinklerSaveConfig (config) {
   var command = new XMLHttpRequest();
   command.open("POST", "/config");
   command.setRequestHeader('Content-Type', 'application/json');
   command.onreadystatechange = function () {
      if (command.readyState === 4 && command.status !== 200) {
         window.alert ('Operation failed (error '+command.status+')!');
      }
   }
   command.send(JSON.stringify(config));
}

function sprinklerStatus (callback) {
   var command = new XMLHttpRequest();
   command.open("GET", "/status");
   command.onreadystatechange = function () {
      if (command.readyState === 4 && command.status === 200) {
         var status = JSON.parse(command.responseText);
         // var type = command.getResponseHeader("Content-Type");
         callback(status);
      }
   }
   command.send(null);
}

function sprinklerHardwareInfo (callback) {
   var command = new XMLHttpRequest();
   command.open("GET", "/hardware/info");
   command.onreadystatechange = function () {
      if (command.readyState === 4 && command.status === 200) {
         var status = JSON.parse(command.responseText);
         // var type = command.getResponseHeader("Content-Type");
         callback(status);
      }
   }
   command.send(null);
}

function sprinklerOnOff () {
   var command = new XMLHttpRequest();
   command.open("GET", "/onoff");
   command.send(null);
}

function sprinklerZoneOn (name, duration) {
   var command = new XMLHttpRequest();
   command.open("GET", "/zone/on?name="+name+"&pulse="+duration);
   command.send(null);
}

function sprinklerZoneOff () {
   var command = new XMLHttpRequest();
   command.open("GET", "/zone/off");
   command.send(null);
}

function sprinklerRefresh () {
}

function sprinklerHistory (callback) {
   var command = new XMLHttpRequest();
   command.open("GET", "/history");
   command.onreadystatechange = function () {
      if (command.readyState === 4 && command.status === 200) {
         var response = JSON.parse(command.responseText);
         // var type = command.getResponseHeader("Content-Type");
         callback(response.history);
      }
   }
   command.send(null);
}

function sprinklerLatestEvent (callback) {
   var command = new XMLHttpRequest();
   command.open("GET", "/history/latest");
   command.onreadystatechange = function () {
      if (command.readyState === 4 && command.status === 200) {
         var event = JSON.parse(command.responseText);
         // var type = command.getResponseHeader("Content-Type");
         callback(event);
      }
   }
   command.send(null);
}

