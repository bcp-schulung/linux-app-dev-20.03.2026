#include "injection.h"
#include <sqlite3.h>
#include <stdio.h>

int import_csv_to_sqlite(sqlite3 *db, const char *filename,
                         const char *table_name) {
  FILE *fp = fopen(filename, "r");
  if (!fp) {
    fprintf(stderr, "Could not open file %s\n", filename);
    return 1;
  }

  sqlite3_stmt *res;
  // Dynamically create the SQL string safely
  char *sql =
      sqlite3_mprintf("INSERT INTO %q (id, name) VALUES (?, ?);", table_name);

  if (sqlite3_prepare_v2(db, sql, -1, &res, 0) != SQLITE_OK) {
    sqlite3_free(sql);
    fclose(fp);
    return 1;
  }

  sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, 0);

  char line[1024];
  int row_count = 0;

  while (fgets(line, sizeof(line), fp)) {
    // Skip the header row
    if (row_count++ == 0)
      continue;

    char *ptr = strtok(line, ",");
    for (int i = 1; i <= 8; i++) {
      if (ptr) {
        // If it's the timestamp (col 1) or station_id (col 2), bind as text
        // Otherwise, bind as double (float) for weather metrics
        if (i == 1 || i == 2) {
          sqlite3_bind_text(res, i, ptr, -1, SQLITE_TRANSIENT);
        } else {
          sqlite3_bind_double(res, i, atof(ptr));
        }
        ptr = strtok(NULL, ",\n");
      }
    }

    sqlite3_step(res);
    sqlite3_reset(res);
  }

  sqlite3_exec(db, "COMMIT;", 0, 0, 0);

  sqlite3_finalize(res);
  sqlite3_free(sql);
  fclose(fp);
  return 0;
}

int readDatabase(sqlite3 *db) {
  // Open CSV file
  if (sqlite3_open("data/fake-station.db", &db) != SQLITE_OK) {
    return 1;
  }

  // Create table with appropriate types
  const char *schema = "CREATE TABLE IF NOT EXISTS weather ("
                       "timestamp TEXT, "
                       "station_id TEXT, "
                       "temperature_c REAL, "
                       "humidity_pct REAL, "
                       "pressure_hpa REAL, "
                       "wind_speed_mps REAL, "
                       "wind_dir_deg REAL, "
                       "rain_mm REAL);";

  // Table schema
  sqlite3_exec(db, schema, 0, 0, 0);

  // Call our modular function
  if (import_csv_to_sqlite(db, "data/fake-station.csv", "users") == 0) {
    printf("Import successful!\n");
  } else {
    printf("Import failed.\n");
  }
  return 0;
}