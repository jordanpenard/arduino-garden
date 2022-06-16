
function drawGraph() {
    $.get( "data.csv", function( csv_data ) {
        var data = $.csv.toArrays(csv_data);
        
        labels = [];
        moisture_data = [];
        pump_data = [];
        
        for (const line of data) {
            labels.push(new Date(line[0]*1000));
            moisture_data.push(line[1]);
            pump_data.push(line[2]);
        }

        const config = {type: 'line',
                        data: {
                            labels: labels,
                            datasets: [{
                                type: 'line',
                                label: 'Soil moisture',
                                data: moisture_data,
                                yAxisID: 'Moisture',
                                fill: false,
                                borderColor: 'rgb(54, 162, 235)',
                                backgroundColor: 'rgb(54, 162, 235)',
                                tension: 0.1
                            }, {
                                type: 'line',
                                label: 'Pump',
                                data: pump_data,
                                yAxisID: 'Pump',
                                fill: true,
                                borderColor: 'rgb(255, 99, 132)',
                                backgroundColor: 'rgba(255, 99, 132, 0.2)',
                                tension: 0.1
                            }]},
                        options: {
                            scales: {
                                Moisture: {
                                    type: 'linear',
                                    position: 'left'},
                                Pump: {
                                    type: 'linear',
                                    position: 'right',
                                    max: 1,
                                    min: 0
                                }
                            }
                        }};
        
        const myChart = new Chart(document.getElementById('myChart'), config);
    });
}
