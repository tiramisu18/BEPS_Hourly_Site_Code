#include "beps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>

#define YEAR 2020

#define PARAMETER_N 50
#define VAR_N 41

#define INPUT_DIR "/Users/jr/Documents/01-Research/06-BEPS-Model/BEPS_Hourly_Site_Code/BEPS_Amazon/input"
#define OUTPUT_DIR "/Users/jr/Documents/01-Research/06-BEPS-Model/BEPS_Hourly_Site_Code/BEPS_Amazon/output/GPP"


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
 * Count pixels in Basic TXT
 * ========================================================= */

static long long count_pixels(const char *filename)
{
    FILE *fp;
    char line[1024];
    PixelInfo p;
    int n;
    long long count = 0;

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
            count++;
        }
    }

    fclose(fp);

    if (count <= 0) {
        fatal_error("No valid pixels found in Basic file.");
    }

    return count;
}


/* =========================================================
 * Read all pixels from Basic TXT
 * ========================================================= */

static void read_basic(const char *filename, PixelInfo *pixels, long long n_pixels)
{
    FILE *fp;
    char line[1024];
    PixelInfo p;
    int n;
    long long index = 0;

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

            if (index >= n_pixels) {
                fclose(fp);
                fatal_error("More pixels found than expected.");
            }

            pixels[index] = p;
            index++;
        }
    }

    fclose(fp);

    if (index != n_pixels) {
        fatal_error("Number of pixels changed while reading Basic file.");
    }
}


/* =========================================================
 * Check numerical value
 * ========================================================= */

static void check_value(const char *name, double value, long long pixel_index, int day, int hour)
{
    if (!isfinite(value)) {
        fprintf(stderr,
                "ERROR: %s is invalid: day=%d hour=%d pixel=%lld value=%f\n",
                name, day, hour, pixel_index, value);
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
 * Check BIN size
 * ========================================================= */

static void check_bin_size(FILE *fp, const char *filename, long long expected_elements)
{
    off_t file_size;
    off_t expected_size;

    file_size = get_file_size(fp);
    expected_size = (off_t)expected_elements * (off_t)sizeof(float);

    printf("%-65s %lld bytes\n", filename, (long long)file_size);

    if (file_size < expected_size) {
        fprintf(stderr,
                "ERROR: BIN file is smaller than expected:\n"
                "       %s\n"
                "       actual   = %lld bytes\n"
                "       expected = %lld bytes\n",
                filename,
                (long long)file_size,
                (long long)expected_size);
        exit(EXIT_FAILURE);
    }
}


/* =========================================================
 * Read a complete time slice from BIN
 *
 * BIN layout:
 *
 * [time0 pixel0]
 * [time0 pixel1]
 * ...
 * [time1 pixel0]
 * [time1 pixel1]
 *
 * Therefore every hourly read is sequential.
 * ========================================================= */

static void read_time_slice(FILE *fp,
                            float *buffer,
                            long long n_pixels,
                            const char *name,
                            int time_index)
{
    size_t nread;

    nread = fread(buffer, sizeof(float), (size_t)n_pixels, fp);

    if (nread != (size_t)n_pixels) {
        fprintf(stderr,
                "\nERROR: Cannot read complete time slice.\n"
                "       Variable = %s\n"
                "       Time     = %d\n"
                "       Expected = %lld values\n"
                "       Read     = %zu values\n",
                name,
                time_index,
                n_pixels,
                nread);
        exit(EXIT_FAILURE);
    }

    for (long long i = 0; i < n_pixels; i++) {
        if (!isfinite((double)buffer[i])) {
            fprintf(stderr,
                    "\nERROR: Invalid %s value.\n"
                    "       Time  = %d\n"
                    "       Pixel = %lld\n"
                    "       Value = %f\n",
                    name,
                    time_index,
                    i,
                    buffer[i]);
            exit(EXIT_FAILURE);
        }
    }
}


/* =========================================================
 * Read one complete LAI day
 * ========================================================= */

static void read_lai_day(FILE *fp,
                         float *buffer,
                         long long n_pixels,
                         const char *name,
                         int day)
{
    size_t nread;

    nread = fread(buffer, sizeof(float), (size_t)n_pixels, fp);

    if (nread != (size_t)n_pixels) {
        fprintf(stderr,
                "\nERROR: Cannot read complete LAI day.\n"
                "       Day      = %d\n"
                "       Expected = %lld values\n"
                "       Read     = %zu values\n",
                day,
                n_pixels,
                nread);
        exit(EXIT_FAILURE);
    }

    for (long long i = 0; i < n_pixels; i++) {
        if (!isfinite((double)buffer[i])) {
            fprintf(stderr,
                    "\nERROR: Invalid LAI value: day=%d pixel=%lld value=%f\n",
                    day,
                    i,
                    buffer[i]);
            exit(EXIT_FAILURE);
        }
    }
}


/* =========================================================
 * Main
 * ========================================================= */

int main(void)
{

    char basic_file[512], lai_file[512];
    char sw_file[512], temp_file[512], rh_file[512], p_file[512], wind_file[512];
    char gpp_file[512];

    snprintf(basic_file, sizeof(basic_file), "%s/basic/BEPS_Basic_%d.txt", INPUT_DIR, YEAR);
    snprintf(lai_file, sizeof(lai_file), "%s/lai/LAI_%d.bin", INPUT_DIR, YEAR);
    snprintf(sw_file, sizeof(sw_file), "%s/meteo/SW/SW_%d.bin", INPUT_DIR, YEAR);
    snprintf(temp_file, sizeof(temp_file), "%s/meteo/Air_Temperature/Air_Temperature_%d.bin", INPUT_DIR, YEAR);
    snprintf(rh_file, sizeof(rh_file), "%s/meteo/RH/RH_%d.bin", INPUT_DIR, YEAR);
    snprintf(p_file, sizeof(p_file), "%s/meteo/P/P_%d.bin", INPUT_DIR, YEAR);
    snprintf(wind_file, sizeof(wind_file), "%s/meteo/WindSpeed/WindSpeed_%d.bin", INPUT_DIR, YEAR);
    snprintf(gpp_file, sizeof(gpp_file), "%s/GPP_%d.bin", OUTPUT_DIR, YEAR);


    FILE *fp_lai = NULL;
    FILE *fp_sw = NULL;
    FILE *fp_temp = NULL;
    FILE *fp_rh = NULL;
    FILE *fp_p = NULL;
    FILE *fp_wind = NULL;
    FILE *fp_output = NULL;

    PixelInfo *pixels = NULL;

    struct Soil *soils = NULL;

    double (*parameters)[PARAMETER_N] = NULL;
    double (*var_o)[VAR_N] = NULL;
    double (*var_n)[VAR_N] = NULL;

    float *lai_buffer = NULL;
    float *sw_buffer = NULL;
    float *temp_buffer = NULL;
    float *rh_buffer = NULL;
    float *p_buffer = NULL;
    float *wind_buffer = NULL;

    float *daily_gpp = NULL;

    struct climatedata meteo;
    struct results mid_res;

    long long n_pixels;

    int days_in_year;
    int day;
    int hour;

    printf("\n");
    printf("============================================================\n");
    printf("                BEPS AMAZON REGIONAL MODEL\n");
    printf("============================================================\n");
    printf("Year: %d\n", YEAR);
    printf("============================================================\n\n");


    /* =====================================================
     * 1. Determine number of pixels
     * ===================================================== */

    printf("[1] Counting pixels...\n");

    n_pixels = count_pixels(basic_file);

    printf("    N pixels = %lld\n\n", n_pixels);


    /* =====================================================
     * 2. Determine number of days
     * ===================================================== */

    days_in_year = 365;
    // days_in_year = 6;

    if ((YEAR % 4 == 0 && YEAR % 100 != 0) || (YEAR % 400 == 0)) {
        // days_in_year = 6;
        days_in_year = 366;
    }

    printf("[2] Time configuration\n");
    printf("    Year       = %d\n", YEAR);
    printf("    Days       = %d\n", days_in_year);
    printf("    Hours      = %d\n", days_in_year * 24);
    printf("    Pixels     = %lld\n\n", n_pixels);


    /* =====================================================
     * 3. Allocate memory
     * ===================================================== */

    printf("[3] Allocating memory...\n");

    pixels = (PixelInfo *)checked_malloc((size_t)n_pixels * sizeof(PixelInfo));

    soils = (struct Soil *)checked_malloc((size_t)n_pixels * sizeof(struct Soil));

    parameters = (double (*)[PARAMETER_N])checked_malloc(
        (size_t)n_pixels * sizeof(*parameters));

    var_o = (double (*)[VAR_N])checked_malloc(
        (size_t)n_pixels * sizeof(*var_o));

    var_n = (double (*)[VAR_N])checked_malloc(
        (size_t)n_pixels * sizeof(*var_n));

    lai_buffer = (float *)checked_malloc((size_t)n_pixels * sizeof(float));
    sw_buffer = (float *)checked_malloc((size_t)n_pixels * sizeof(float));
    temp_buffer = (float *)checked_malloc((size_t)n_pixels * sizeof(float));
    rh_buffer = (float *)checked_malloc((size_t)n_pixels * sizeof(float));
    p_buffer = (float *)checked_malloc((size_t)n_pixels * sizeof(float));
    wind_buffer = (float *)checked_malloc((size_t)n_pixels * sizeof(float));

    daily_gpp = (float *)checked_malloc((size_t)n_pixels * sizeof(float));

    printf("    Memory allocation completed.\n\n");


    /* =====================================================
     * 4. Read Basic
     * ===================================================== */

    printf("[4] Reading Basic information...\n");

    read_basic(basic_file, pixels, n_pixels);

    printf("    First pixel:\n");
    printf("      ID        = %lld\n", pixels[0].pixel_id);
    printf("      row       = %d\n", pixels[0].row);
    printf("      col       = %d\n", pixels[0].col);
    printf("      lon       = %.8f\n", pixels[0].lon);
    printf("      lat       = %.8f\n", pixels[0].lat);
    printf("      landcover = %d\n", pixels[0].landcover);
    printf("      clumping  = %.6f\n", pixels[0].clumping);
    printf("      soil_type = %d\n", pixels[0].soil_type);

    printf("\n    Last pixel:\n");
    printf("      ID        = %lld\n", pixels[n_pixels - 1].pixel_id);
    printf("      row       = %d\n", pixels[n_pixels - 1].row);
    printf("      col       = %d\n", pixels[n_pixels - 1].col);
    printf("      lon       = %.8f\n", pixels[n_pixels - 1].lon);
    printf("      lat       = %.8f\n", pixels[n_pixels - 1].lat);

    printf("\n");


    /* =====================================================
     * 5. Read BEPS parameters for every pixel
     *
     * Parameters depend mainly on land-cover type.
     * We prepare them once and reuse them every hour.
     * ===================================================== */

    printf("[5] Initializing BEPS parameters...\n");

    for (long long i = 0; i < n_pixels; i++) {

        memset(parameters[i], 0, PARAMETER_N * sizeof(double));

        readparam((short)pixels[i].landcover, parameters[i]);
    }

    printf("    Parameter initialization completed.\n\n");


    /* =====================================================
     * 6. Open input files
     * ===================================================== */

    printf("[6] Opening input files...\n");

    fp_lai = fopen(lai_file, "rb");
    fp_sw = fopen(sw_file, "rb");
    fp_temp = fopen(temp_file, "rb");
    fp_rh = fopen(rh_file, "rb");
    fp_p = fopen(p_file, "rb");
    fp_wind = fopen(wind_file, "rb");

    if (fp_lai == NULL) {
        perror(lai_file);
        exit(EXIT_FAILURE);
    }

    if (fp_sw == NULL) {
        perror(sw_file);
        exit(EXIT_FAILURE);
    }

    if (fp_temp == NULL) {
        perror(temp_file);
        exit(EXIT_FAILURE);
    }

    if (fp_rh == NULL) {
        perror(rh_file);
        exit(EXIT_FAILURE);
    }

    if (fp_p == NULL) {
        perror(p_file);
        exit(EXIT_FAILURE);
    }

    if (fp_wind == NULL) {
        perror(wind_file);
        exit(EXIT_FAILURE);
    }

    printf("    All input files opened successfully.\n\n");


    /* =====================================================
     * 7. Check BIN sizes
     * ===================================================== */

    printf("[7] Checking BIN files...\n");

    check_bin_size(
        fp_lai,
        lai_file,
        (long long)days_in_year * n_pixels
    );

    check_bin_size(
        fp_sw,
        sw_file,
        (long long)days_in_year * 24LL * n_pixels
    );

    check_bin_size(
        fp_temp,
        temp_file,
        (long long)days_in_year * 24LL * n_pixels
    );

    check_bin_size(
        fp_rh,
        rh_file,
        (long long)days_in_year * 24LL * n_pixels
    );

    check_bin_size(
        fp_p,
        p_file,
        (long long)days_in_year * 24LL * n_pixels
    );

    check_bin_size(
        fp_wind,
        wind_file,
        (long long)days_in_year * 24LL * n_pixels
    );

    printf("\n");


    /* =====================================================
     * 8. Initialize state arrays
     * ===================================================== */

    printf("[8] Initializing BEPS state arrays...\n");

    memset(var_o, 0, (size_t)n_pixels * sizeof(*var_o));
    memset(var_n, 0, (size_t)n_pixels * sizeof(*var_n));
    memset(soils, 0, (size_t)n_pixels * sizeof(struct Soil));

    printf("    State arrays initialized.\n\n");


    /* =====================================================
     * 9. Read first-hour air temperature
     *
     * This is needed for soil initialization.
     *
     * The temperature BIN is time-major:
     *
     * hour 0 -> all pixels
     * hour 1 -> all pixels
     * ...
     * ===================================================== */

    printf("[9] Reading initial air temperature...\n");

    read_time_slice(
        fp_temp,
        temp_buffer,
        n_pixels,
        "Air Temperature",
        0
    );

    printf("    First pixel Tair = %.6f C\n",
           (double)temp_buffer[0]);

    printf("\n");


    /* =====================================================
     * 10. Initialize soil for every pixel
     *
     * IMPORTANT:
     *
     * Soil is initialized ONLY ONCE at:
     *
     * Year 1, Day 1, Hour 0
     *
     * After that the BEPS soil state is continuously updated.
     * ===================================================== */

    printf("[10] Initializing soil states...\n");

    for (long long i = 0; i < n_pixels; i++) {

        Init_Soil_Parameters(
            pixels[i].landcover,
            pixels[i].soil_type,
            parameters[i][27],
            &soils[i]
        );

        Init_Soil_Status(
            &soils[i],
            pixels[i].soiltemp,
            (double)temp_buffer[i],
            pixels[i].soilwater,
            pixels[i].snowdp
        );
    }

    printf("    Soil initialization completed for %lld pixels.\n\n",
           n_pixels);


    /* =====================================================
     * 11. Rewind temperature file
     *
     * We already read hour 0 above, but the actual simulation
     * starts from hour 0, so rewind all meteorological files.
     * ===================================================== */

    rewind(fp_temp);


    /* =====================================================
     * 12. Open output file
     * ===================================================== */

    printf("[11] Opening output file...\n");

    fp_output = fopen(gpp_file, "wb");

    if (fp_output == NULL) {
        perror(gpp_file);
        exit(EXIT_FAILURE);
    }

    printf("    Output: %s\n\n", gpp_file);


    /* =====================================================
     * 13. Main simulation loop
     *
     * Structure:
     *
     * year
     *   day
     *     read LAI
     *     hour
     *       read meteorology
     *       pixel
     *         BEPS
     *         accumulate GPP
     * ===================================================== */

    printf("============================================================\n");
    printf("                    START SIMULATION\n");
    printf("============================================================\n\n");

    for (day = 1; day <= days_in_year; day++) {

        printf("Day %3d / %3d", day, days_in_year);
        fflush(stdout);


        /* -----------------------------------------------------
         * Read one complete LAI day
         * ----------------------------------------------------- */

        read_lai_day(
            fp_lai,
            lai_buffer,
            n_pixels,
            "LAI",
            day
        );


        /* -----------------------------------------------------
         * Reset daily GPP
         * ----------------------------------------------------- */

        memset(
            daily_gpp,
            0,
            (size_t)n_pixels * sizeof(float)
        );


        /* -----------------------------------------------------
         * 24 hourly simulations
         * ----------------------------------------------------- */

        for (hour = 0; hour < 24; hour++) {

            /* -------------------------------------------------
             * Read one complete hourly meteorological slice
             * ------------------------------------------------- */

            read_time_slice(
                fp_sw,
                sw_buffer,
                n_pixels,
                "SW",
                (day - 1) * 24 + hour
            );

            read_time_slice(
                fp_temp,
                temp_buffer,
                n_pixels,
                "Air Temperature",
                (day - 1) * 24 + hour
            );

            read_time_slice(
                fp_rh,
                rh_buffer,
                n_pixels,
                "RH",
                (day - 1) * 24 + hour
            );

            read_time_slice(
                fp_p,
                p_buffer,
                n_pixels,
                "Precipitation",
                (day - 1) * 24 + hour
            );

            read_time_slice(
                fp_wind,
                wind_buffer,
                n_pixels,
                "Wind Speed",
                (day - 1) * 24 + hour
            );


            /* -------------------------------------------------
             * Process every pixel
             * ------------------------------------------------- */

            for (long long i = 0; i < n_pixels; i++) {

                double lai;
                double CosZs;


                /* ---------------------------------------------
                 * LAI
                 *
                 * Same calculation as the validated
                 * single-pixel version:
                 *
                 * lai = lai_raw * parameter[2] / clumping
                 * --------------------------------------------- */

                if (lai_buffer[i] < 0.0f) {
                    lai = 0.0;
                }
                else {

                    if (pixels[i].clumping > 0.0) {
                        lai = (double)lai_buffer[i] *
                              parameters[i][2] /
                              pixels[i].clumping;
                    }
                    else {
                        lai = 0.0;
                    }
                }


                /* ---------------------------------------------
                 * Construct meteorological structure
                 *
                 * Units:
                 *
                 * SW   = W m-2
                 * T    = degree C
                 * RH   = %
                 * P    = m hour-1
                 * Wind = m s-1
                 * --------------------------------------------- */

                memset(
                    &meteo,
                    0,
                    sizeof(struct climatedata)
                );

                meteo.Srad = (double)sw_buffer[i];

                /* Let BEPS calculate longwave radiation */
                meteo.LR = -200.0;

                meteo.temp = (double)temp_buffer[i];
                meteo.rh = (double)rh_buffer[i];
                meteo.rain = (double)p_buffer[i];
                meteo.wind = (double)wind_buffer[i];

                meteo.dr_o = 0.0;
                meteo.df_o = 0.0;
                meteo.dr_u = 0.0;
                meteo.df_u = 0.0;


                /* ---------------------------------------------
                 * Solar geometry
                 *
                 * IMPORTANT:
                 *
                 * hour = 0...23 is UTC hour.
                 *
                 * The s_coszs.c currently used in the validated
                 * single-pixel test must contain:
                 *
                 * hr = j * 24.0 / RTIMES + lon / 15.0;
                 * --------------------------------------------- */

                s_coszs(
                    (short)day,
                    (short)hour,
                    (float)pixels[i].lat,
                    (float)pixels[i].lon,
                    &CosZs
                );


                /* ---------------------------------------------
                 * BEPS calculation
                 * --------------------------------------------- */

                memset(
                    &mid_res,
                    0,
                    sizeof(struct results)
                );

                inter_prg(
                    day,
                    hour,
                    lai,
                    pixels[i].clumping,
                    parameters[i],
                    &meteo,
                    CosZs,
                    var_o[i],
                    var_n[i],
                    &soils[i],
                    &mid_res
                );


                /* ---------------------------------------------
                 * Accumulate hourly GPP
                 * --------------------------------------------- */

                if (!isfinite(mid_res.GPP)) {
                    fprintf(stderr,
                            "\nERROR: Invalid GPP.\n"
                            "       Day    = %d\n"
                            "       Hour   = %d\n"
                            "       Pixel  = %lld\n"
                            "       Lon    = %.8f\n"
                            "       Lat    = %.8f\n"
                            "       GPP    = %f\n",
                            day,
                            hour,
                            i,
                            pixels[i].lon,
                            pixels[i].lat,
                            mid_res.GPP);
                    exit(EXIT_FAILURE);
                }

                daily_gpp[i] += (float)mid_res.GPP;


                /* ---------------------------------------------
                 * Update BEPS state
                 *
                 * This is CRITICAL.
                 *
                 * Each pixel must maintain its own state.
                 * --------------------------------------------- */

                memcpy(
                    var_o[i],
                    var_n[i],
                    VAR_N * sizeof(double)
                );
            }
        }


        /* -----------------------------------------------------
         * Write one complete day to output
         *
         * Output layout:
         *
         * day0 pixel0
         * day0 pixel1
         * ...
         * day1 pixel0
         * ...
         * ----------------------------------------------------- */

        if (fwrite(
                daily_gpp,
                sizeof(float),
                (size_t)n_pixels,
                fp_output
            ) != (size_t)n_pixels) {

            fprintf(stderr,
                    "\nERROR: Failed to write daily GPP.\n"
                    "       Day = %d\n",
                    day);

            exit(EXIT_FAILURE);
        }


        /* -----------------------------------------------------
         * Daily diagnostics
         * ----------------------------------------------------- */

        double mean_gpp = 0.0;
        double max_gpp = -1.0e30;
        double min_gpp = 1.0e30;

        for (long long i = 0; i < n_pixels; i++) {

            double gpp = (double)daily_gpp[i];

            mean_gpp += gpp;

            if (gpp > max_gpp) {
                max_gpp = gpp;
            }

            if (gpp < min_gpp) {
                min_gpp = gpp;
            }
        }

        mean_gpp /= (double)n_pixels;

        printf(" | GPP mean = %.4f | min = %.4f | max = %.4f\n",
               mean_gpp,
               min_gpp,
               max_gpp);

        fflush(stdout);
    }


    /* =====================================================
     * 14. Close files
     * ===================================================== */

    fclose(fp_lai);
    fclose(fp_sw);
    fclose(fp_temp);
    fclose(fp_rh);
    fclose(fp_p);
    fclose(fp_wind);
    fclose(fp_output);


    /* =====================================================
     * 15. Final message
     * ===================================================== */

    printf("\n");
    printf("============================================================\n");
    printf("                  SIMULATION FINISHED\n");
    printf("============================================================\n");
    printf("Year       : %d\n", YEAR);
    printf("Days       : %d\n", days_in_year);
    printf("Pixels     : %lld\n", n_pixels);
    printf("Output     : %s\n", gpp_file);
    printf("Output type: float32\n");
    printf("Output dim : [%d days, %lld pixels]\n",
           days_in_year,
           n_pixels);
    printf("============================================================\n");


    /* =====================================================
     * 16. Free memory
     * ===================================================== */

    free(pixels);
    free(soils);
    free(parameters);
    free(var_o);
    free(var_n);

    free(lai_buffer);
    free(sw_buffer);
    free(temp_buffer);
    free(rh_buffer);
    free(p_buffer);
    free(wind_buffer);

    free(daily_gpp);

    return 0;
}