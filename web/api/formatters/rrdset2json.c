// SPDX-License-Identifier: GPL-3.0-or-later

#include "rrdset2json.h"

// generate JSON for the /api/v1/chart API call

void rrdset2json(RRDSET *st, BUFFER *wb, size_t *dimensions_count, size_t *memory_used, int skip_volatile) {
    rrdset_rdlock(st);

#ifdef SQLITE_POC
    time_t first_entry_t = st->state->first_entry_t;
    time_t last_entry_t = rrdset_last_entry_t(st);
#else
    time_t first_entry_t = rrdset_first_entry_t(st);
    time_t last_entry_t  = rrdset_last_entry_t(st);
#endif

    buffer_sprintf(wb,
            "\t\t{\n"
            "\t\t\t\"id\": \"%s\",\n"
            "\t\t\t\"name\": \"%s\",\n"
            "\t\t\t\"type\": \"%s\",\n"
            "\t\t\t\"family\": \"%s\",\n"
            "\t\t\t\"context\": \"%s\",\n"
            "\t\t\t\"title\": \"%s (%s)\",\n"
            "\t\t\t\"priority\": %ld,\n"
            "\t\t\t\"plugin\": \"%s\",\n"
            "\t\t\t\"module\": \"%s\",\n"
            "\t\t\t\"enabled\": %s,\n"
            "\t\t\t\"units\": \"%s\",\n"
            "\t\t\t\"data_url\": \"/api/v1/data?chart=%s\",\n"
            "\t\t\t\"chart_type\": \"%s\",\n"
                    , st->id
                    , st->name
                    , st->type
                    , st->family
                    , st->context
                    , st->title, st->name
                    , st->priority
                    , st->plugin_name?st->plugin_name:""
                    , st->module_name?st->module_name:""
                    , rrdset_flag_check(st, RRDSET_FLAG_ENABLED)?"true":"false"
                    , st->units
                    , st->name
                    , rrdset_type_name(st->chart_type)
    );

    if (likely(!skip_volatile))
        buffer_sprintf(wb,
                "\t\t\t\"duration\": %ld,\n"
            , last_entry_t - first_entry_t + st->update_every//st->entries * st->update_every
        );

    buffer_sprintf(wb,
                "\t\t\t\"first_entry\": %ld,\n"
        , first_entry_t //rrdset_first_entry_t(st)
    );

    if (likely(!skip_volatile))
        buffer_sprintf(wb,
                "\t\t\t\"last_entry\": %ld,\n"
            , last_entry_t//rrdset_last_entry_t(st)
        );

    buffer_sprintf(wb,
                "\t\t\t\"update_every\": %d,\n"
                "\t\t\t\"dimensions\": {\n"
                       , st->update_every
        );

    unsigned long memory = st->memsize;

    size_t dimensions = 0;
    RRDDIM *rd;
    rrddim_foreach_read(rd, st) {
        if(rrddim_flag_check(rd, RRDDIM_FLAG_HIDDEN) || rrddim_flag_check(rd, RRDDIM_FLAG_OBSOLETE)) continue;

        memory += rd->memsize;

        if (dimensions)
            buffer_strcat(wb, ",\n\t\t\t\t\"");
        else
            buffer_strcat(wb, "\t\t\t\t\"");
        buffer_strcat_jsonescape(wb, rd->id);
        buffer_strcat(wb, "\": { \"name\": \"");
        buffer_strcat_jsonescape(wb, rd->name);
        buffer_strcat(wb, "\" }");

        dimensions++;
    }

#ifdef SQLITE_POC
     if (rrdset_flag_check(st, RRDSET_FLAG_ARCHIVED)) {
//         char sql[512];
//        struct dimension *dimension_list = NULL, *tmp_dimension_list;
//        int from_row, to_row;
//        if (st->state->to) {
//            from_row = st->state->from;
//            to_row = st->state->to;
//            int max_rows = sql_select_dimension(st->chart_uuid, &dimension_list, NULL, NULL);
//        }
//        else {
//            int max_rows = sql_select_dimension(st->chart_uuid, &dimension_list, &from_row, &to_row);
//            st->state->from = from_row;
//            st->state->to = to_row;
//        }
        int old_dimensions = dimensions;
        int duration = sql_load_one_chart_dimension(st->chart_uuid, wb, &dimensions);


//        while (dimension_list) {
//            if (dimensions)
//                buffer_strcat(wb, ",\n\t\t\t\t\"");
//            else
//                buffer_strcat(wb, "\t\t\t\t\"");
//            buffer_strcat_jsonescape(wb, dimension_list->id);
//            buffer_strcat(wb, "\": { \"name\": \"");
//            buffer_strcat_jsonescape(wb, dimension_list->name);
//            buffer_strcat(wb, " (");
//            buffer_strcat(wb, dimension_list->dim_str);
//            buffer_strcat(wb, ")");
//            buffer_strcat(wb, "\" }");
//
//            tmp_dimension_list = dimension_list->next;
//            freez(dimension_list->id);
//            freez(dimension_list->name);
//            freez(dimension_list);
//            dimension_list = tmp_dimension_list;
//            dimensions++;
//        }
//        freez(dimension_list);
        //info("SQLITE: Processed %d dimensions in %d usec", dimensions - old_dimensions, duration);
    }
#endif

    if(dimensions_count) *dimensions_count += dimensions;
    if(memory_used) *memory_used += memory;

    buffer_sprintf(wb, "\n\t\t\t},\n\t\t\t\"chart_variables\": ");
    health_api_v1_chart_custom_variables2json(st, wb);

    buffer_strcat(wb, ",\n\t\t\t\"green\": ");
    buffer_rrd_value(wb, st->green);
    buffer_strcat(wb, ",\n\t\t\t\"red\": ");
    buffer_rrd_value(wb, st->red);

    if (likely(!skip_volatile)) {
        buffer_strcat(wb, ",\n\t\t\t\"alarms\": {\n");
        size_t alarms = 0;
        RRDCALC *rc;
        for (rc = st->alarms; rc; rc = rc->rrdset_next) {
            buffer_sprintf(
                wb,
                "%s"
                "\t\t\t\t\"%s\": {\n"
                "\t\t\t\t\t\"id\": %u,\n"
                "\t\t\t\t\t\"status\": \"%s\",\n"
                "\t\t\t\t\t\"units\": \"%s\",\n"
                "\t\t\t\t\t\"update_every\": %d\n"
                "\t\t\t\t}",
                (alarms) ? ",\n" : "", rc->name, rc->id, rrdcalc_status2string(rc->status), rc->units,
                rc->update_every);

            alarms++;
        }
        buffer_sprintf(wb,
                       "\n\t\t\t}"
        );
    }

    buffer_sprintf(wb,
            "\n\t\t}"
    );

    rrdset_unlock(st);
}
