# The map service: the land of the world (map_land_data.c, made by tools/perinkpheral map), its
# projections and its drawing. Main firmware only: recovery does not list this component.
idf_component_register(SRCS "map_geo.c" "map_land.c" "map_land_data.c" "map_draw.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx)
