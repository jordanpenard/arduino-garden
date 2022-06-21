
function drawGraph() {
    $.get("data.csv", function( csv_data ) {
        var data_array = $.csv.toArrays(csv_data);
        
        date_data = [];
        moisture_data = [];
        pump_data = [];
        humidity_data = [];
        temperature_data = [];
        for (const line of data_array) {
            date_data.push(line[0]*1000);
            moisture_data.push(line[1]);
            pump_data.push(line[2]);
            humidity_data.push(line[3]);
            temperature_data.push(line[4]);
        }

        const moisture_trace = {
            type: "scatter",
            mode: "lines",
            name: 'Soil moisture',
            x: date_data,
            y: moisture_data,
            line: {color: 'rgb(54, 162, 235)'}
        };
          
        const pump_trace = {
          type: "scatter",
          mode: "lines",
          name: 'Pump',
          x: date_data,
          y: pump_data,
          yaxis: 'y2',
          fill: 'tozeroy',
          fillcolor: 'rgba(255, 99, 132, 0.2)',
          line: {color: 'rgb(255, 99, 132)', shape: 'hv'}
        };
      
        const humidity_trace = {
          type: "scatter",
          mode: "lines",
          name: 'Air humidity (%)',
          x: date_data,
          y: humidity_data,
          yaxis: 'y3',
          line: {color: 'rgb(44, 160, 44)'}
        };
    
        const temperature_trace = {
          type: "scatter",
          mode: "lines",
          name: 'Air temperature (C)',
          x: date_data,
          y: temperature_data,
          yaxis: 'y4',
          line: {color: '#ff7f0e'}
        };
  
        const data = [moisture_trace, pump_trace, humidity_trace, temperature_trace];

        var OneDaysAgo = new Date(new Date().setDate(new Date().getDate() - 1));

        const layout = {
            title: 'Arduino Garden data logging',
            xaxis: {
              autorange: true,
              rangeselector: {buttons: [
                  {
                    count: 1,
                    label: 'day',
                    step: 'day',
                    stepmode: 'backward',
                    default: true
                  },
                  {
                    count: 7,
                    label: 'week',
                    step: 'day',
                    stepmode: 'backward'
                  },
                  {
                    count: 1,
                    label: 'month',
                    step: 'month',
                    stepmode: 'backward'
                  },
                  {
                    count: 1,
                    label: 'year',
                    step: 'year',
                    stepmode: 'backward'
                  },
                  {step: 'all'}
                ]},
              rangeslider: {autorange: true},
              domain: [0, 0.95],
              type: 'date'
            },
            yaxis: {
              tickfont: {color: 'rgb(54, 162, 235)'},
              autorange: true,
              type: 'linear'
            },
            yaxis2: {
              autorange: false,
              range: [0, 1],
              overlaying: 'y',
              showline: false,
              showgrid: false,
              showticklabels: false,
              type: 'linear'
            },
            yaxis3: {
              tickfont: {color: 'rgb(44, 160, 44)'},
              autorange: true,
              overlaying: 'y',
              side: 'right'
            },
            yaxis4: {
              tickfont: {color: '#ff7f0e'},
              autorange: true,
              showgrid: false,
              overlaying: 'y',
              side: 'right',
              position: 1
            }
        };
          
        Plotly.newPlot('myChart', data, layout);
    });
}

function loadConfig() {
    $.get("config.json", function( config_json ) {
        for (var key in config_json) {   
            $('#'+key).val(config_json[key]);
        }
    });
}

function saveConfig() {
  request = "setConfig?";

  for (var key of ["watering_intervals_in_hours", "watering_duration_in_seconds", "moisture_threashold", "history_steps_in_seconds", "password"]) {
    request += key + "=" + $('#configPannel #'+key).val() + "&";
  }

  $.ajax({
    url: request,
    type: 'GET',
    success: function(data){ 
      $('#configPannel').trigger('click');
    },
    error: function(xhr, textStatus, errorThrown) {
      alert('Error: ' + xhr.responseText);
    }
  });
}

function changePassword() {
  request = "setConfig?";
  
  if ($('#new_password').val() != $('#new_password_repeat').val()) {
    alert("New passwords don't match");
  } else {

    for (var key of ["password", "new_password"]) {
      request += key + "=" + $('#changePasswordPannel #'+key).val() + "&";
    }

    $.ajax({
      url: request,
      type: 'GET',
      success: function(data){ 
        $('#changePasswordPannel').trigger('click');
      },
      error: function(xhr, textStatus, errorThrown) {
        alert('Error: ' + xhr.responseText);
      }
    });
  }
}

function manualWatering() {
  request = "manualWatering?password=" + $('#manualWateringdPannel #password').val() ;

  $.ajax({
    url: request,
    type: 'GET',
    success: function(data){ 
      $('#manualWateringdPannel').trigger('click');
    },
    error: function(xhr, textStatus, errorThrown) {
      alert('Error: ' + xhr.responseText);
    }
  });
}

function reboot() {
  request = "reboot?password=" + $('#rebootPannel #password').val() ;

  $.ajax({
    url: request,
    type: 'GET',
    success: function(data){ 
      $('#rebootPannel').trigger('click');
    },
    error: function(xhr, textStatus, errorThrown) {
      alert('Error: ' + xhr.responseText);
    }
  });
}

$(document).ready(function() {
    drawGraph();
    loadConfig();
});
