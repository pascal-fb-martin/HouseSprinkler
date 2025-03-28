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
//   sprinklerRainDelay (duration);
//   sprinklerCancelRainDelay ();
//
//      These functions control the rain delay function: add a rain delay,
//      and cancel any pending rain delay.
//
//   sprinklerIndexToggle ();
//
//      This function toggles the watering index on and off. When the index
//      is off, all programs run at 100%.
//
//   sprinklerRefresh();
//
//      This function requests the controler to refresh all information
//      from the outsid world: weather, calendar programs.
//
//   sprinklerEvents(callback);
//
//      This function requests the complete event log.
//
//   sprinklerLatestEvent(callback);
//
//      This function requests the ID of the latest event.
//

var SprinklerUseIndex = false;

function sprinklerShowDuration (seconds) {
   var minutes = Math.floor(seconds / 60);
   seconds = Math.floor(seconds % 60);
   if (minutes > 60) {
      var hours = Math.floor(minutes / 60);
      if (hours > 48) {
          var days = Math.floor(hours/24);
          hours = Math.floor(hours % 24);
          if (hours > 0)
              return days+' Days '+hours+' Hours';
          return days+' Days';
      }
      minutes = Math.floor(minutes % 60);
      return ('00'+hours).slice(-2)+':'+('00'+minutes).slice(-2);
   }
   return '00:'+('00'+minutes).slice(-2);
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
   var program;

   sprinklerSetContent ('hostname', response.host);
   var portal = document.getElementById('portal');
   if (portal) {
       portal.href = 'http://'+response.proxy+'/index.html';
   }

   if (response.sprinkler.program.active) {
      program = response.sprinkler.program.active.join(', ');
   } else {
      program = null;
   }
   if (response.sprinkler.zone.active) {
      content = response.sprinkler.zone.active+' ACTIVE';
      if (! program) program = 'MANUAL';
   } else {
      content = 'IDLE';
      if (! program) program = 'IDLE';
   }

   if (response.sprinkler.schedule.on == false) {
      program = 'OFF';
   }
   sprinklerSetContent ('activeprogram', program);
   sprinklerSetContent ('activezone', content);

   if (response.sprinkler.schedule.raindelay == null) {
      content = 'DISABLED';
   } else if (response.sprinkler.schedule.raindelay > 0) {
      var deadline = response.sprinkler.schedule.raindelay * 1000;
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

   SprinklerUseIndex = response.sprinkler.program.useindex;

   if (response.sprinkler.program.useindex) {
      sprinklerSetContent ('adjustment',
          ''+response.sprinkler.index.value+'%'+' FROM '+response.sprinkler.index.origin);
   } else {
      sprinklerSetContent ('adjustment','DISABLED');
   }

   var title = response.host + ' - Sprinkler Controler';
   document.getElementsByTagName ('title')[0].innerHTML = title;

   for (var i = 0; i < response.sprinkler.zone.zones.length; ++i) {
       var zone = response.sprinkler.zone.zones[i];
       var label = document.getElementById ('zone_'+i+'_label');
       if (label) {
           if (zone[2]) {
               label.innerHTML = zone[0]+' @'+zone[2];
           } else {
               label.innerHTML = zone[0];
           }
       }
   }

   for (var i = 0; i < response.sprinkler.schedule.schedules.length; ++i) {
       var schedule = response.sprinkler.schedule.schedules[i];
       if (! schedule.launched) continue;
       var latest = document.getElementById (schedule.id+'_latest');
       if (latest) {
           var now = new Date(schedule.launched * 1000);
           latest.innerHTML = now.toDateString();
       }
   }
}

function sprinklerRequest (uri) {
   var command = new XMLHttpRequest();
   command.open("GET", uri);
   command.onreadystatechange = function () {
       if (command.readyState === 4 && command.status === 200) {
           sprinklerApplyUpdate(command.responseText);
       }
   }
   command.send(null);
}

function sprinklerUpdate () {
   sprinklerRequest ("/sprinkler/status");
}

function sprinklerInfo () {
   sprinklerUpdate();
   setInterval (sprinklerUpdate, 1000);
}

function sprinklerConfig (callback) {
   var command = new XMLHttpRequest();
   command.open("GET", "/sprinkler/config");
   command.onreadystatechange = function () {
      if (command.readyState === 4 && command.status === 200) {
         var config = JSON.parse(command.responseText);
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
         callback(config);
      }
   }
   command.send(null);
}

function sprinklerSaveConfig (config) {
   var command = new XMLHttpRequest();
   command.open("POST", "/sprinkler/config");
   command.setRequestHeader('Content-Type', 'application/json');
   command.onreadystatechange = function () {
      if (command.readyState === 4) {
          if (command.status === 200) {
             window.alert ('Configuration Saved.');
          } else {
             window.alert ('Operation failed (error '+command.status+')!');
          }
      }
   }
   command.send(JSON.stringify(config));
}

function sprinklerStatus (callback) {
   var command = new XMLHttpRequest();
   command.open("GET", "/sprinkler/status");
   command.onreadystatechange = function () {
      if (command.readyState === 4 && command.status === 200) {
         var status = JSON.parse(command.responseText);
         callback(status);
      }
   }
   command.send(null);
}

function sprinklerHardwareInfo (callback) {
}

function sprinklerOnOff () {
   sprinklerRequest ("/sprinkler/onoff");
}

function sprinklerZoneOn (name, duration) {
   sprinklerRequest("/sprinkler/zone/on?name="+name+"&pulse="+duration);
}

function sprinklerZoneOff () {
   sprinklerRequest ("/sprinkler/zone/off");
}

function sprinklerCancelRainDelay () {
   sprinklerRequest ("/sprinkler/raindelay?amount=0");
}

function sprinklerIndexToggle () {
   var makeActive = 'true';
   if (SprinklerUseIndex) makeActive = 'false';
   sprinklerRequest ("/sprinkler/index?active="+makeActive);
}

function sprinklerRainDelay (duration) {
   if (duration)
       sprinklerRequest ("/sprinkler/raindelay?amount="+duration);
   else
       sprinklerRequest ("/sprinkler/raindelay");
}

function sprinklerRefresh () {
   sprinklerRequest ("/sprinkler/refresh");
}

function sprinklerEvents (callback) {
   var command = new XMLHttpRequest();
   command.open("GET", "/sprinkler/log/events");
   command.onreadystatechange = function () {
      if (command.readyState === 4 && command.status === 200) {
         var response = JSON.parse(command.responseText);
         callback(response);
      }
   }
   command.send(null);
}

function sprinklerLatestEvent (callback) {
   var command = new XMLHttpRequest();
   command.open("GET", "/sprinkler/log/latest");
   command.onreadystatechange = function () {
      if (command.readyState === 4 && command.status === 200) {
         var response = JSON.parse(command.responseText);
         callback(response.sprinkler.latest);
      }
   }
   command.send(null);
}

