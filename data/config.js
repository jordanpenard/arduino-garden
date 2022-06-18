
function loadConfig() {
    $.get("config.json", function( config_json ) {
        var config = JSON.parse(config_json)
        for (var key in config) {   
            $('[name="'+key+'"]').val(config[key]);
        }
    });
}
