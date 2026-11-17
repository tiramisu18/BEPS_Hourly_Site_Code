#include "beps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>

#define YEAR 2000
#define DAY 1

/* =========================================================
 * Test pixel
 * PIXEL_INDEX = 0 means the first pixel
 * ========================================================= */
#define PIXEL_INDEX 0
#define N_PIXELS 11003

/* =========================================================
 * Input files
 * ========================================================= */
#define BASIC_FILE "/Users/jr/Documents/01-Research/06-BEPS-Model/BEPS_Hourly_Site_Code/BEPS_Amazon/input/basic/BEPS_Basic_2000.txt"
#define LAI_FILE "/Users/jr/Documents/01-Research/06-BEPS-Model/BEPS_Hourly_Site_Code/BEPS_Amazon/input/lai/LAI_2000.bin"

#define SW_FILE "/Users/jr/Documents/01-Research/06-BEPS-Model/BEPS_Hourly_Site_Code/BEPS_Amazon/input/meteo/SW/SW_2000.bin"
#define TEMP_FILE "/Users/jr/Documents/01-Research/06-BEPS-Model/BEPS_Hourly_Site_Code/BEPS_Amazon/input/meteo/Air_Temperature/Air_Temperature_2000.bin"
#define RH_FILE "/Users/jr/Documents/01-Research/06-BEPS-Model/BEPS_Hourly_Site_Code/BEPS_Amazon/input/meteo/RH/RH_2000.bin"
#define P_FILE "/Users/jr/Documents/01-Research/06-BEPS-Model/BEPS_Hourly_Site_Code/BEPS_Amazon/input/meteo/P/P_2000.bin"
#define WIND_FILE "/Users/jr/Documents/01-Research/06-BEPS-Model/BEPS_Hourly_Site_Code/BEPS_Amazon/input/meteo/WindSpeed/WindSpeed_2000.bin"

/* =========================================================
 * BEPS array sizes
 * ========================================================= */
#define PARAMETER_N 50
#define VAR_N 41


/* =========================================================
 * Pixel information
 * ========================================================= */
typedef struct {
    long long pixel_id;
    int row;
    int col;
    double lon;
    double lat;
    int landcover;
    double clumping;
    int soil_type;
    double soiltemp;
    double soilwater;
    double snowdp;
} PixelInfo;


/* =========================================================
 * Fatal error
 * ========================================================= */
static void fatal_error(const char *msg)
{
    fprintf(stderr, "\nERROR: %s\n", msg);
    exit(EXIT_FAILURE);
}


/* =========================================================
 * Checked malloc
 * ========================================================= */
static void *checked_malloc(size_t n)
{
    void *p = malloc(n);

    if (p == NULL) {
        fatal_error("Memory allocation failed.");
    }

    return p;
}


/* =========================================================
 * Read first pixel from Basic TXT
 *
 * The first line may be a header.
 * ========================================================= */
static PixelInfo read_first_pixel_from_basic(const char *filename)
{
    FILE *fp;
    char line[1024];
    PixelInfo p;
    int n;

    fp = fopen(filename, "r");

    if (fp == NULL) {
        perror(filename);
        exit(EXIT_FAILURE);
    }

    while (fgets(line, sizeof(line), fp) != NULL) {

        n = sscanf(line,
                   "%lld %d %d %lf %lf %d %lf %d %lf %lf %lf",
                   &p.pixel_id,
                   &p.row,
                   &p.col,
                   &p.lon,
                   &p.lat,
                   &p.landcover,
                   &p.clumping,
                   &p.soil_type,
                   &p.soiltemp,
                   &p.soilwater,
                   &p.snowdp);

        if (n == 11) {
            fclose(fp);
            return p;
        }
    }

    fclose(fp);

    fatal_error("No valid pixel data found in Basic file.");

    /* Only to satisfy compiler */
    return p;
}


/* =========================================================
 * Check numerical value
 * ========================================================= */
static void check_value(const char *name, double value)
{
    if (!isfinite(value)) {
        fprintf(stderr, "ERROR: %s = %f\n", name, value);
        exit(EXIT_FAILURE);
    }
}


/* =========================================================
 * Get file size
 * ========================================================= */
static off_t get_file_size(FILE *fp)
{
    off_t current;
    off_t size;

    current = ftello(fp);

    if (current < 0) {
        fatal_error("Cannot get current file position.");
    }

    if (fseeko(fp, 0, SEEK_END) != 0) {
        fatal_error("Cannot seek to end of file.");
    }

    size = ftello(fp);

    if (size < 0) {
        fatal_error("Cannot determine file size.");
    }

    if (fseeko(fp, current, SEEK_SET) != 0) {
        fatal_error("Cannot restore file position.");
    }

    return size;
}


/* =========================================================
 * Check BIN file size
 *
 * expected_elements:
 *     number of float32 values expected
 * ========================================================= */
static void check_bin_size(FILE *fp, const char *filename, long long expected_elements)
{
    off_t file_size;
    off_t expected_size;

    file_size = get_file_size(fp);
    expected_size = (off_t)expected_elements * (off_t)sizeof(float);

    printf("%s\n", filename);
    printf("  file size     = %lld bytes\n", (long long)file_size);
    printf("  expected size = %lld bytes\n", (long long)expected_size);

    if (file_size < expected_size) {
        fprintf(stderr,
                "ERROR: BIN file is smaller than expected: %s\n",
                filename);
        exit(EXIT_FAILURE);
    }

    printf("  size check    = OK\n\n");
}


/* =========================================================
 * Read one float from a [time][pixel] BIN file
 *
 * BIN layout:
 *
 * [time0 pixel0]
 * [time0 pixel1]
 * ...
 * [time0 pixelN]
 * [time1 pixel0]
 * [time1 pixel1]
 * ...
 *
 * Therefore:
 *
 * index = time_index * N_PIXELS + pixel_index
 * ========================================================= */
static float read_bin_value(FILE *fp,
                            int time_index,
                            int pixel_index,
                            const char *name)
{
    off_t index;
    off_t offset;
    float value;

    index = (off_t)time_index * (off_t)N_PIXELS + (off_t)pixel_index;
    offset = index * (off_t)sizeof(float);

    if (fseeko(fp, offset, SEEK_SET) != 0) {
        fprintf(stderr,
                "ERROR: Cannot seek %s: time=%d pixel=%d offset=%lld\n",
                name,
                time_index,
                pixel_index,
                (long long)offset);
        exit(EXIT_FAILURE);
    }

    if (fread(&value, sizeof(float), 1, fp) != 1) {
        fprintf(stderr,
                "ERROR: Cannot read %s: time=%d pixel=%d offset=%lld\n",
                name,
                time_index,
                pixel_index,
                (long long)offset);
        exit(EXIT_FAILURE);
    }

    if (!isfinite((double)value)) {
        fprintf(stderr,
                "ERROR: Invalid %s value: time=%d pixel=%d value=%f\n",
                name,
                time_index,
                pixel_index,
                value);
        exit(EXIT_FAILURE);
    }

    return value;
}


/* =========================================================
 * Main
 * ========================================================= */
int main(void)
{
    FILE *fp_lai;
    FILE *fp_sw;
    FILE *fp_temp;
    FILE *fp_rh;
    FILE *fp_p;
    FILE *fp_wind;

    PixelInfo pixel;

    float lai_raw;
    float sw_raw;
    float temp_raw;
    float rh_raw;
    float p_raw;
    float wind_raw;

    double lai;
    double CosZs;
    double daily_gpp = 0.0;

    double parameter[PARAMETER_N] = {0.0};
    double var_o[VAR_N] = {0.0};
    double var_n[VAR_N] = {0.0};

    struct Soil soil;
    struct climatedata meteo;
    struct results mid_res;

    int hour;

    printf("========================================\n");
    printf(" BEPS regional single-pixel test\n");
    printf("========================================\n");
    printf("Year       : %d\n", YEAR);
    printf("Day        : %d\n", DAY);
    printf("Pixel      : %d (first pixel)\n", PIXEL_INDEX + 1);
    printf("Pixel index: %d\n", PIXEL_INDEX);
    printf("N pixels   : %d\n", N_PIXELS);
    printf("Hours      : 24\n");
    printf("========================================\n\n");


    /* =========================================================
     * 1. Read first pixel from Basic
     * ========================================================= */

    pixel = read_first_pixel_from_basic(BASIC_FILE);

    printf("[Basic]\n");
    printf("pixel_id   = %lld\n", pixel.pixel_id);
    printf("row        = %d\n", pixel.row);
    printf("col        = %d\n", pixel.col);
    printf("lon        = %.8f\n", pixel.lon);
    printf("lat        = %.8f\n", pixel.lat);
    printf("landcover  = %d\n", pixel.landcover);
    printf("clumping   = %.6f\n", pixel.clumping);
    printf("soil_type  = %d\n", pixel.soil_type);
    printf("soiltemp   = %.6f C\n", pixel.soiltemp);
    printf("soilwater  = %.6f\n", pixel.soilwater);
    printf("snowdp     = %.6f\n", pixel.snowdp);
    printf("\n");


    /* =========================================================
     * 2. Read BEPS land-cover parameters
     * ========================================================= */

    readparam((short)pixel.landcover, parameter);

    printf("[BEPS parameters]\n");
    printf("parameter[2]  = %.6f\n", parameter[2]);
    printf("parameter[4]  = %.6f\n", parameter[4]);
    printf("parameter[36] = %.6f\n", parameter[36]);
    printf("parameter[37] = %.6f\n", parameter[37]);
    printf("parameter[46] = %.6f\n", parameter[46]);
    printf("parameter[47] = %.6f\n", parameter[47]);
    printf("\n");


    /* =========================================================
     * 3. Open all BIN files
     * ========================================================= */

    fp_lai = fopen(LAI_FILE, "rb");
    fp_sw = fopen(SW_FILE, "rb");
    fp_temp = fopen(TEMP_FILE, "rb");
    fp_rh = fopen(RH_FILE, "rb");
    fp_p = fopen(P_FILE, "rb");
    fp_wind = fopen(WIND_FILE, "rb");

    if (fp_lai == NULL) {
        perror(LAI_FILE);
        exit(EXIT_FAILURE);
    }

    if (fp_sw == NULL) {
        perror(SW_FILE);
        exit(EXIT_FAILURE);
    }

    if (fp_temp == NULL) {
        perror(TEMP_FILE);
        exit(EXIT_FAILURE);
    }

    if (fp_rh == NULL) {
        perror(RH_FILE);
        exit(EXIT_FAILURE);
    }

    if (fp_p == NULL) {
        perror(P_FILE);
        exit(EXIT_FAILURE);
    }

    if (fp_wind == NULL) {
        perror(WIND_FILE);
        exit(EXIT_FAILURE);
    }


    /* =========================================================
     * 4. Check BIN sizes
     *
     * LAI:
     *     365/366 days × N_PIXELS
     *
     * Meteorology:
     *     8760/8784 hours × N_PIXELS
     *
     * Here we only check that the files contain enough data
     * for the first 24 hours / first day.
     * ========================================================= */

    printf("[BIN size check]\n");

    check_bin_size(
        fp_lai,
        LAI_FILE,
        (long long)N_PIXELS
    );

    check_bin_size(
        fp_sw,
        SW_FILE,
        24LL * N_PIXELS
    );

    check_bin_size(
        fp_temp,
        TEMP_FILE,
        24LL * N_PIXELS
    );

    check_bin_size(
        fp_rh,
        RH_FILE,
        24LL * N_PIXELS
    );

    check_bin_size(
        fp_p,
        P_FILE,
        24LL * N_PIXELS
    );

    check_bin_size(
        fp_wind,
        WIND_FILE,
        24LL * N_PIXELS
    );


    /* =========================================================
     * 5. Read first-hour air temperature
     *
     * Needed for soil initialization.
     *
     * time index = 0
     * pixel index = PIXEL_INDEX
     * ========================================================= */

    temp_raw = read_bin_value(
        fp_temp,
        0,
        PIXEL_INDEX,
        "Air Temperature"
    );

    printf("[Initial air temperature]\n");
    printf("Tair(2000-01-01 00:00) = %.6f C\n",
           (double)temp_raw);
    printf("\n");


    /* =========================================================
     * 6. Initialize soil
     * ========================================================= */

    memset(&soil, 0, sizeof(struct Soil));

    Init_Soil_Parameters(
        pixel.landcover,
        pixel.soil_type,
        parameter[27],
        &soil
    );

    Init_Soil_Status(
        &soil,
        pixel.soiltemp,
        (double)temp_raw,
        pixel.soilwater,
        pixel.snowdp
    );

    printf("[Soil initialized]\n");
    printf("soil initialization completed.\n");
    printf("\n");


    /* =========================================================
     * 7. Read first-day LAI
     *
     * LAI BIN layout:
     *
     * [day0 pixel0]
     * [day0 pixel1]
     * ...
     *
     * DAY=1 and PIXEL_INDEX=0:
     *
     * index = (DAY - 1) * N_PIXELS + PIXEL_INDEX
     *       = 0
     * ========================================================= */

    lai_raw = read_bin_value(
        fp_lai,
        DAY - 1,
        PIXEL_INDEX,
        "LAI"
    );

    check_value("LAI", (double)lai_raw);

    printf("[LAI]\n");
    printf("LAI raw day %d = %.6f\n",
           DAY,
           (double)lai_raw);

    if (lai_raw < 0.0f) {
        fprintf(stderr,
                "WARNING: LAI is invalid: %.6f\n",
                (double)lai_raw);
    }

    printf("\n");


    /* =========================================================
     * 8. Run 24 hourly BEPS simulations
     * ========================================================= */

    printf("[Hourly BEPS simulation]\n\n");

    for (hour = 0; hour < 24; hour++) {

        /* -----------------------------------------------------
         * Read the correct pixel from each [hour][pixel] BIN
         *
         * index = hour * N_PIXELS + PIXEL_INDEX
         * ----------------------------------------------------- */

        sw_raw = read_bin_value(
            fp_sw,
            hour,
            PIXEL_INDEX,
            "SW"
        );

        temp_raw = read_bin_value(
            fp_temp,
            hour,
            PIXEL_INDEX,
            "Air Temperature"
        );

        rh_raw = read_bin_value(
            fp_rh,
            hour,
            PIXEL_INDEX,
            "RH"
        );

        p_raw = read_bin_value(
            fp_p,
            hour,
            PIXEL_INDEX,
            "Precipitation"
        );

        wind_raw = read_bin_value(
            fp_wind,
            hour,
            PIXEL_INDEX,
            "Wind Speed"
        );


        /* -----------------------------------------------------
         * Check values
         * ----------------------------------------------------- */

        check_value("SW", (double)sw_raw);
        check_value("Air Temperature", (double)temp_raw);
        check_value("RH", (double)rh_raw);
        check_value("Precipitation", (double)p_raw);
        check_value("Wind Speed", (double)wind_raw);


        /* -----------------------------------------------------
         * Construct BEPS meteorological structure
         *
         * Current BIN units:
         *
         * SW   -> W m-2
         * T    -> degree C
         * RH   -> %
         * P    -> m hour-1
         * Wind -> m s-1
         *
         * Therefore no additional unit conversion is performed.
         * ----------------------------------------------------- */

        memset(&meteo, 0, sizeof(struct climatedata));

        meteo.Srad = (double)sw_raw;
        meteo.LR = -200.0;
        meteo.temp = (double)temp_raw;
        meteo.rh = (double)rh_raw;
        meteo.rain = (double)p_raw;
        meteo.wind = (double)wind_raw;

        meteo.dr_o = 0.0;
        meteo.df_o = 0.0;
        meteo.dr_u = 0.0;
        meteo.df_u = 0.0;


        /* -----------------------------------------------------
         * Solar zenith angle
         * ----------------------------------------------------- */

        s_coszs(
            (short)DAY,
            (short)hour,
            (float)pixel.lat,
            (float)pixel.lon,
            &CosZs
        );


        /* -----------------------------------------------------
         * LAI used by BEPS
         *
         * Same as original point driver:
         *
         * lai = lai_p[jday] * parameter[2] / clumping;
         * ----------------------------------------------------- */

        if (lai_raw < 0.0f) {
            lai = 0.0;
        } else {
            lai = (double)lai_raw * parameter[2] / pixel.clumping;
        }


        /* -----------------------------------------------------
         * Run BEPS
         * ----------------------------------------------------- */

        memset(&mid_res, 0, sizeof(struct results));

        inter_prg(
            DAY,
            hour,
            lai,
            pixel.clumping,
            parameter,
            &meteo,
            CosZs,
            var_o,
            var_n,
            &soil,
            &mid_res
        );


        /* -----------------------------------------------------
         * Check GPP
         * ----------------------------------------------------- */

        check_value("Hourly GPP", mid_res.GPP);


        /* -----------------------------------------------------
         * Accumulate hourly GPP
         * ----------------------------------------------------- */

        daily_gpp += mid_res.GPP;


        /* -----------------------------------------------------
         * Update BEPS state
         * ----------------------------------------------------- */

        memcpy(
            var_o,
            var_n,
            VAR_N * sizeof(double)
        );


        /* -----------------------------------------------------
         * Print diagnostics
         * ----------------------------------------------------- */

        printf("hour %02d | "
               "SW=%10.4f | "
               "T=%8.3f | "
               "RH=%7.3f | "
               "P=%10.6f | "
               "WS=%7.3f | "
               "LAI=%7.3f | "
               "CosZ=%8.5f | "
               "GPP=%10.6f\n",
               hour,
               meteo.Srad,
               meteo.temp,
               meteo.rh,
               meteo.rain,
               meteo.wind,
               lai,
               CosZs,
               mid_res.GPP);
    }


    /* =========================================================
     * 9. Close files
     * ========================================================= */

    fclose(fp_lai);
    fclose(fp_sw);
    fclose(fp_temp);
    fclose(fp_rh);
    fclose(fp_p);
    fclose(fp_wind);


    /* =========================================================
     * 10. Final result
     * ========================================================= */

    printf("\n========================================\n");
    printf("Simulation finished.\n");
    printf("========================================\n");
    printf("Year       : %d\n", YEAR);
    printf("Day        : %d\n", DAY);
    printf("Pixel ID   : %lld\n", pixel.pixel_id);
    printf("Location   : %.8f, %.8f\n",
           pixel.lon,
           pixel.lat);
    printf("Landcover  : %d\n", pixel.landcover);
    printf("Daily GPP  : %.8f g C m-2 day-1\n",
           daily_gpp);
    printf("========================================\n");

    return 0;
}

