package base_pkg;

    typedef logic [31:0] data_width_t;

    typedef struct packed {
        logic [7:0] version;
        logic [15:0] features;
        data_width_t max_width;
    } config_t;

    // Import and export from util_pkg to create circular dependency
    import util_pkg::result_t;
    import util_pkg::SUCCESS;
    import util_pkg::ERROR_CODE;
    export util_pkg::result_t;
    export util_pkg::SUCCESS;
    export util_pkg::ERROR_CODE;
    export util_pkg::create_config;

    function automatic config_t get_default_config();
        config_t cfg;
        cfg.version = 8'h01;
        cfg.features = 16'hFFFF;
        cfg.max_width = 32;
        return cfg;
    endfunction

    function automatic util_pkg::result_t validate_config(input config_t cfg);
        if (cfg.max_width > 0)
            return util_pkg::SUCCESS;
        else
            return util_pkg::FAILURE;
    endfunction

endpackage
