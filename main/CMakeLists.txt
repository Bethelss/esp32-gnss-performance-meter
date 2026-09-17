idf_component_register(
    SRCS "main.c"
         "gnss/gnss.c"
         "gnss/ubx_parser.c"
         "gnss/ubx_protocol.c"
         "gnss/ubx_config.c"
         "display.c"
    INCLUDE_DIRS "." "gnss")
