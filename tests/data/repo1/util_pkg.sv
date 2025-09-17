package util_pkg;

    typedef enum logic [1:0] {
        SUCCESS = 2'b00,
        FAILURE = 2'b01,
        PENDING = 2'b10,
        ERROR_CODE = 2'b11
    } result_t;

    // Import and export from base_pkg to create circular dependency
    import base_pkg::config_t;
    import base_pkg::data_width_t;
    export base_pkg::config_t;
    export base_pkg::get_default_config;

    function automatic result_t process_width(input base_pkg::data_width_t width);
        if (width >= 8 && width <= 64)
            return SUCCESS;
        else
            return FAILURE;
    endfunction

    function automatic base_pkg::config_t create_config(input base_pkg::data_width_t width);
        base_pkg::config_t cfg = base_pkg::get_default_config();
        cfg.max_width = width;
        return cfg;
    endfunction

endpackage
