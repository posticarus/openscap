/*
 * OVAL simple application for evaluating OVAL definition file.
 * The output is system characteristic xml file and oval results xml file.
 * Program iterate through tests in every definition and evaluate objects
 * of these tests.
 *
 * Copyright 2008 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors:
 *      Maros Barabas  <mbarabas@redhat.com>

 * Help:
 *      compile: $gcc oscap-scan.c -lcurl -lopenscap -I/usr/include/openscap/
 *               or
 *               $gcc -I../src/OVAL/public/ -I../src/common/public/ oscap-scan.c -L../src/.libs/ -lcurl -lopenscap
 *      run:     $./oscap-scan -v definition_file.xml --result-file=results.xml --syschar-file=syschars.xml
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

/* curl */
#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>

/* lstat and errno */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

/* OSCAP */
#include <oval_probe.h>
#include <oval_agent_api.h>
#include <oval_results.h>
#include <error.h>


void print_usage(const char *pname, FILE *out);
int app_evaluate_test(struct oval_test * test, oval_pctx_t * pctx, 
                      struct oval_definition_model *def_model, 
                      struct oval_syschar_model *sys_model,
                      int verbose);
int app_evaluate_criteria(struct oval_criteria_node *cnode, oval_pctx_t * pctx, 
                          struct oval_definition_model *def_model, 
                          struct oval_syschar_model *sys_model,
                          int verbose);
char * app_curl_download(char * url);


/**
 * Function print usage of this program to specified output
 * @param pname name of program, standard usage argv[0]
 * @param out output stream for fprintf function, standard usage stdout or stderr
 */
void print_usage(const char *pname, FILE *out) {

  fprintf(out, 
	  "Usage: %s [options] [url|path/]filename\n"
          "\n"
          "Evaluate OVAL definition file specified by filename.\n"
          "The input source may be either the full path to the xml file "
          "or an URL from which to download it.\n"
          "\n"
          "Options:\n"
	  "   -h --help\r\t\t\t\t - show this help\n"
	  "   -v --verbose <integer>\r\t\t\t\t - run in verbose mode (0,1,2)\n"
	  "   -q --quiet\r\t\t\t\t - Quiet mode. Suppress all warning and messages.\n"
          "   -o --objects-only\r\t\t\t\t - evaluate objects only, ignore definitions "
             " \n\r\t\t\t\t   and verbosity\n"
	  "   --result-file <file>\r\t\t\t\t - The name of file with OVAL results output.\n"
	  "   --syschar-file <file>\r\t\t\t\t - The name of file with Syschar output.\n"
	  ,pname);
}

/**
 * Function evaluating tests. First get object from test and try if it's already
 * evaluated. If not, process the evaluation.
 * @param test oval_test structure which is being evaluated
 * @param pctx oval_pctx structure for probe process
 * @param def_model Definition model
 * @param sys_model System characteristic model
 * @param verbose Verbosity level
 * @returns -1 in case of error, 0 in case of warning and 1 in case of success
 */
int app_evaluate_test(struct oval_test * test, oval_pctx_t * pctx, 
                      struct oval_definition_model *def_model, 
                      struct oval_syschar_model *sys_model,
                      int verbose) {

    char                    *objid;
    struct oval_object      *object;
    struct oval_syschar     *syschar = NULL;

    object = oval_test_get_object(test);
    
    /* Check if there is an object to evaluate */
    if (object != NULL) {
        objid = oval_object_get_id(object);
        /* Is the object already evaluated ? */
        syschar = oval_syschar_model_get_syschar(sys_model, objid);
        if (syschar == NULL) {
            /* NO it's not done yet, continue .. */
            if (verbose == 2) fprintf(stdout, "Probing object (%s)\n", objid);
            syschar = oval_probe_object_eval (pctx, object);
            /* There is a problem with evaluating .. */
            if (syschar == NULL) {
                    if (verbose >= 1) fprintf(stdout, "WARNING: Syschar for object (%s) is not available\n", objid);
                    if( oscap_err() ) {
                            if (verbose >= 0) printf("Error: (%d) %s\n", oscap_err_code(), oscap_err_desc());
                             /* does it make sense to continue? depends on error code */
                    }
                    syschar = oval_syschar_new(sys_model, object);
                    oval_syschar_set_flag(syschar,SYSCHAR_FLAG_NOT_COLLECTED);
            }
            /* Everything OK, add characteristic for this object to syschar model */
            oval_syschar_model_add_syschar(sys_model, syschar);
        /* Object is already evaluated and present in syschar model */
        } else if (verbose == 2) fprintf(stdout, "Object (%s) already done, skipping\n", objid);
    /* Probably we are on unknown_test which does noto contain objects */
    } else if (verbose == 2) fprintf(stdout, "Test (%s) contains no objects\n", oval_test_get_id(test));

    return 1;
}

/**
 * Function that takes criteria node, find it's type and then goes recursively deeper
 * to criteria XML tree to evaluate all criterions and extended definitions by calling
 * app_evaluate_test on final criterion node.
 * @param cnode oval_criteria_node structure represents criteria, criterion and 
 *              extended definition node
 * @param pctx oval_pctx structure for probe process
 * @param def_model Definition model
 * @param sys_model System characteristic model
 * @param verbose Verbosity level
 * @returns -1 in case of error, 0 in case of warning and 1 in case of success
 */
int app_evaluate_criteria(struct oval_criteria_node *cnode, oval_pctx_t * pctx, 
                          struct oval_definition_model *def_model, 
                          struct oval_syschar_model *sys_model,
                          int verbose) {

    int ret = -1;
    switch (oval_criteria_node_get_type(cnode)) {
        /* Criterion node is final node that has reference to test */
        case OVAL_NODETYPE_CRITERION:{
            /* There should be a test .. */
            struct oval_test * test = oval_criteria_node_get_test(cnode);
            if (test == NULL) {
                oscap_seterr(OSCAP_EFAMILY_OSCAP, OSCAP_EUSER1, "Criteria node has no test");
                return -1;
            }
            /* .. evaluate it and return */
            return app_evaluate_test(test, pctx, def_model, sys_model, verbose);

        } break;
        /* Criteria node is type of set that contains more criterias. Criteria node
         * child can be also type of criteria, criterion or extended definition */
        case OVAL_NODETYPE_CRITERIA:{
            /* group of criterion nodes, get subnodes, continue recursive */
            struct oval_criteria_node_iterator * cnode_it = oval_criteria_node_get_subnodes(cnode);
            if (cnode_it == NULL) {
                oscap_seterr(OSCAP_EFAMILY_OSCAP, OSCAP_EUSER1, "Node CRITERIA should have subnodes");
                return -1;
            }
            /* we have subnotes */
            struct oval_criteria_node * node;
            while (oval_criteria_node_iterator_has_more(cnode_it)) {
                node = oval_criteria_node_iterator_next(cnode_it);
                ret = app_evaluate_criteria(node, pctx, def_model, sys_model, verbose);
                if (ret < 0) {
                    oval_criteria_node_iterator_free(cnode_it);
                    return ret;
                }
            }
            oval_criteria_node_iterator_free(cnode_it);
        } break;
        /* Extended definition contains reference to definition, we need criteria of this
         * definition to be evaluated completely */
        case OVAL_NODETYPE_EXTENDDEF:{
            struct oval_definition * oval_def = oval_criteria_node_get_definition(cnode);
            cnode = oval_definition_get_criteria(oval_def);
            return app_evaluate_criteria(cnode, pctx, def_model, sys_model, verbose);
        } break;
        case OVAL_NODETYPE_UNKNOWN:
            oscap_seterr(OSCAP_EFAMILY_OSCAP, OSCAP_EUSER1, "UNKNOWN Node type");
            break;
    }
    return ret;
}

/**
 * Function with CURL support to download file from remote storage. 
 * Function takes url (which can be path to local file), test if we can stat the file. If 
 * the file is local just returns the path otherwise starts downloading process and 
 * returns name of local stored file.
 * Tested on HTTP, FTP protocols.
 * @param url URL or PATH to file
 * @return path to local file
 */
char * app_curl_download(char * url) {

    struct stat buf;
    /* Is the file local ? */
    if (lstat(url, &buf) == 0 ) return url;

    /* Remote file will be stored in this xml */
    char * outfile = "definition_file.xml";

    CURL *curl;
    FILE *fp;
    CURLcode res;
    /* Initialize CURL for download remote file */
    curl = curl_easy_init();
    if (curl) {
        fp = fopen(outfile,"wb");
        /* Set options for download file to *fp* from *url* */
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        res = curl_easy_perform(curl);
        if (res != 0) {/* Something bad happend */
                oscap_seterr(OSCAP_EFAMILY_OSCAP, OSCAP_EUSER1, "CURL library initialization failed");
                return NULL;
        }
        /* Always cleanup */
        curl_easy_cleanup(curl);
        /* Close the file finally*/
        fclose(fp);
    } else {
        oscap_seterr(OSCAP_EFAMILY_OSCAP, OSCAP_EUSER1, "CURL library initialization failed");
        return NULL;
    }

    return outfile;
}

/**
 * Main function
 */
int main(int argc, char **argv)
{
        int verbose         = 0;    /**< Verbosity level variable */
        int method          = 0;    /**< Method 1 - iterate objects; 
                                                0 - iterate definitions */
        char * url_OVAL     = NULL; /**< URL of OVAL definition file */
        //char * url_XCCDF    = NULL; /**< URL of OVAL definition file */
        oval_pctx_t * pctx  = NULL; /**< */
        char * f_OVAL       = NULL; /**< Name of OVAL definition file*/
        //char * f_XCCDF       = NULL; /**< Name of XCCDF benchmark file*/
        char * f_Results    = NULL;
        char * f_Syschar    = NULL;

        /**************** GETOPT  ***************/
        int c;

        while (1) {

            int option_index = 0;
            static struct option long_options[] = {
                /* Long options. */
                {"quiet",        0, 0, 'q'},
                {"objects-only", 0, 0, 'o'},
                {"help",         0, 0, 'h'},
                {"result-file",  1, 0, 0},
                {"syschar-file", 1, 0, 1},
                {"verbose",      1, 0, 2},
                {0, 0, 0, 0}
            };

            c = getopt_long(argc, argv, "qohv0123",
                        long_options, &option_index);
            if (c == -1)
                break;

            switch (c) {

            case 0: /* Results */
                f_Results = strdup(optarg);
                break;

            case 1: /* Syschars */
                f_Syschar = strdup(optarg);
                break;

            case 2: /* Verbose */
                if (verbose != -1) verbose = atoi(optarg);
                if (verbose > 2) verbose = 2;
                break;

            case 'v':
                /* quiet is higher priority then verbose */
                if (verbose != -1) verbose += 1;
                if (verbose > 2) verbose = 2;
                break;

            case 'q':
                verbose = -1;
                break;

            case 'o':
                method = 1;
                break;

            case 'h': /* Help */
                print_usage(argv[0], stderr);
                return 0;

            default:
                fprintf(stderr, "Bad usage of %s !\n\n", argv[0]);
                print_usage(argv[0], stderr);
                return 1;
            }
        }

        if (optind < argc) /* mandatory OVAL file */
           url_OVAL = strdup(argv[optind++]);

        if (optind < argc) { /* No other not-optional argument */
                fprintf(stderr, "Bad usage of %s !\n\n", argv[0]);
                print_usage(argv[0], stderr);
                return 1;
        }

        /* Post processing of options*/
        /* fetch file from remote source */
        if (url_OVAL == NULL) {
                fprintf(stderr, "Bad usage of %s !\n\n", argv[0]);
                print_usage(argv[0], stderr);
                return 1;
        }

        f_OVAL = app_curl_download(url_OVAL);
        if (!f_OVAL) {
		if (verbose >= 0) fprintf(stderr, "Error: (%d) %s\n", oscap_err_code(), oscap_err_desc());
                return 1;
        }

        /* If results file and syschar file is not specified, we export it anyway */
        /*if (f_Results == NULL) {
            f_Results = malloc( sizeof(char) * strlen(f_OVAL) + 15 );
            sprintf(f_Results, "%s.results.xml", f_OVAL);
        }

        if (f_Syschar == NULL) {
            f_Syschar = malloc( sizeof(char) * strlen(f_OVAL) + 15 );
            sprintf(f_Syschar, "%s.syschar.xml", f_OVAL);
        }*/

        if (verbose >= 2) {
            printf("Running options:\n");
            printf("\tOption Verbose:\r\t\t\t %d\n",  verbose);
            printf("\tOption Method:\r\t\t\t %d\n",   method);
            printf("\tOption OVAL:\r\t\t\t %s\n",     url_OVAL);
            printf("\tOption Results:\r\t\t\t %s\n",  f_Results);
            printf("\tOption Syschar:\r\t\t\t %s\n",  f_Syschar);
        }
        /**************** GETOPT ***************/

        /* TODO: prevent fail behaviour by validating OVAL content */
        /* Note: XCCDF is not supported yet */

        /* Get definition model from XML file */
        /* Set import source for OVAL definition file*/
        struct oscap_import_source *def_in = oscap_import_source_new_file(f_OVAL, NULL);
        struct oval_definition_model *def_model = oval_definition_model_new();
        free(f_OVAL);
        oval_definition_model_import(def_model, def_in, NULL);
        /* Import problems ? Do not continue then ! */
	if( oscap_err() ) {
		if (verbose >= 0) fprintf(stderr, "Error: (%d) %s\n",oscap_err_code(), oscap_err_desc());
                oscap_import_source_free(def_in);
		return 1;
	}
        oscap_import_source_free(def_in);

	/* create syschar model */
	struct oval_syschar_model *sys_model = oval_syschar_model_new(def_model);
        
        pctx = oval_pctx_new(sys_model);
	/* probe sysinfo */
	struct oval_sysinfo *sysinfo;
        sysinfo = oval_probe_sysinf_eval(pctx);
	if( sysinfo == NULL ) {
		if (verbose >= 1) fprintf(stdout, "Warning: sysinfo not available\n");
		if( oscap_err() ) {
			if (verbose >= 0) fprintf(stderr, "Error: (%d) %s\n", oscap_err_code(), oscap_err_desc());
			return 1;
		}
	}
	oval_syschar_model_set_sysinfo(sys_model, sysinfo);
	oval_sysinfo_free(sysinfo);

        int ret = 0; 
	if (method == 1) { 
            /* First method - evaluate objects only */
            oval_syschar_model_probe_objects(sys_model);

        } else {
            /* Get the list of definitions */
            struct oval_definition_iterator * oval_def_it = oval_definition_model_get_definitions(def_model);

            struct oval_criteria_node * cnode = NULL;
            struct oval_definition * oval_def = NULL;

            /* Get number of definitions */
            int def_count = 0;
            while (oval_definition_iterator_has_more(oval_def_it) ) {
                (void) oval_definition_iterator_next(oval_def_it);
                def_count++;
            }
            oval_definition_iterator_free(oval_def_it);

            /* Reset iterator */
            oval_def_it = oval_definition_model_get_definitions(def_model);
            /* Iterate through definitions and evaluate all criteria */
            int curr = 0;
            while (oval_definition_iterator_has_more(oval_def_it) ) {
                curr++;
                oval_def = oval_definition_iterator_next(oval_def_it);

                if (verbose == 0) fprintf(stdout, "\rEvaluating definition %d5 of %d5", curr, def_count);
                else if (verbose >= 1 ) fprintf(stdout, "Evaluating definition: %s\n", oval_definition_get_title(oval_def));

                cnode = oval_definition_get_criteria(oval_def);
                ret = app_evaluate_criteria(cnode, pctx, def_model, sys_model, verbose);
                if (ret == -1) break;
            }
            if (verbose == 0) printf("\nEvaluation: All done.\n");
            oval_definition_iterator_free(oval_def_it);
        }
        oval_pctx_free (pctx);

        if (ret == -1) {
            if (( oscap_err() ) && (verbose >= 0)) 
                if (verbose >=0) fprintf(stderr, "Error: (%d) %s\n", oscap_err_code(), oscap_err_desc());
            return 1;
        }
	/* print # syschars */
        int count = 0;
        struct oval_syschar_iterator *syschars = oval_syschar_model_get_syschars(sys_model);
        for (count = 0; oval_syschar_iterator_has_more(syschars); count++) {
        	oval_syschar_iterator_next(syschars);
        }
        oval_syschar_iterator_free(syschars);

        if (f_Syschar != NULL) {
            /* Export syschar model to XML */
            struct oscap_export_target *syschar_out  = oscap_export_target_new_file(f_Syschar, "UTF-8");
            oval_syschar_model_export(sys_model, syschar_out);
            free(f_Syschar);
            oscap_export_target_free(syschar_out);
        }

	/* create result model */
	struct oval_syschar_model *sys_models[] = {sys_model, NULL};
        struct oval_results_model* res_model = oval_results_model_new( def_model, sys_models );

	/* Evaluate gathered system characteristics */
	oval_results_model_eval(res_model);

        /* Output all results */
        struct oval_result_system               * rsystem       = NULL;
        struct oval_result_system_iterator      * rsystem_it    = NULL;
        struct oval_result_definition           * definition    = NULL;
        struct oval_result_definition_iterator  * definition_it = NULL;
        struct oval_definition                  * odefinition   = NULL;
        oval_result_t result;
        int result_false    = 0;
        int result_true     = 0;
        int result_invalid  = 0;
        int result_unknown  = 0;
        int result_neval    = 0;
        int result_napp     = 0;

        rsystem_it = oval_results_model_get_systems(res_model);
        rsystem = oval_result_system_iterator_next(rsystem_it); /**< Only first system here */

        definition_it = oval_result_system_get_definitions(rsystem);
        while (oval_result_definition_iterator_has_more(definition_it)) {
            definition = oval_result_definition_iterator_next(definition_it);
            odefinition = oval_result_definition_get_definition(definition);
            result = oval_result_definition_get_result(definition);
            switch (result) {
                case OVAL_RESULT_TRUE:{
                            result_true++;
                                      } break;
                case OVAL_RESULT_FALSE:{
                            result_false++;
                                      } break;
                case OVAL_RESULT_INVALID:{
                            result_invalid++;
                                      } break;
                case OVAL_RESULT_UNKNOWN:{
                            result_unknown++;
                                      } break;
                case OVAL_RESULT_NOT_EVALUATED:{
                            result_neval++;
                                      } break;
                case OVAL_RESULT_NOT_APPLICABLE:{
                            result_napp++;
                                      } break;
                default: break;
            }

            if (verbose >= 0)
                fprintf(stdout, "Definition \"%s\": %s\n", oval_definition_get_title(odefinition), 
                                                           oval_result_get_text(result));
        }
        oval_result_definition_iterator_free(definition_it);
        oval_result_system_iterator_free(rsystem_it);

        if (verbose >= 2) {
            fprintf(stdout, "====== RESULTS ======\n");
            fprintf(stdout, "TRUE:          \r\t\t %d\n", result_true);
            fprintf(stdout, "FALSE:         \r\t\t %d\n", result_false);
            fprintf(stdout, "INVALID:       \r\t\t %d\n", result_invalid);
            fprintf(stdout, "UNKNOWN:       \r\t\t %d\n", result_unknown);
            fprintf(stdout, "NOT EVALUATED: \r\t\t %d\n", result_neval);
            fprintf(stdout, "NOT APPLICABLE:\r\t\t %d\n", result_napp);
        }

        if (f_Results != NULL) {

            /* set up directives */
            struct oval_result_directives * res_direct = oval_result_directives_new(res_model);
            oval_result_directives_set_reported(res_direct, OVAL_RESULT_INVALID, true);
            oval_result_directives_set_reported(res_direct, OVAL_RESULT_TRUE, true);
            oval_result_directives_set_reported(res_direct, OVAL_RESULT_FALSE, true);
            oval_result_directives_set_reported(res_direct, OVAL_RESULT_UNKNOWN, true);
            oval_result_directives_set_reported(res_direct, OVAL_RESULT_ERROR, true);
            oval_result_directives_set_reported(res_direct, OVAL_RESULT_NOT_EVALUATED, true);
            oval_result_directives_set_reported(res_direct, OVAL_RESULT_NOT_APPLICABLE , true);
            oval_result_directives_set_content(res_direct,OVAL_RESULT_FALSE, OVAL_DIRECTIVE_CONTENT_FULL);
            oval_result_directives_set_content(res_direct,OVAL_RESULT_TRUE, OVAL_DIRECTIVE_CONTENT_FULL);
            
            /* Export result model to XML */
	    struct oscap_export_target *result_out  = oscap_export_target_new_file(f_Results, "UTF-8");
	    oval_results_model_export(res_model, res_direct, result_out);
	    free(f_Results);
	    oscap_export_target_free(result_out);

            oval_result_directives_free(res_direct);
        }

        oval_definition_model_free(def_model);
        oval_syschar_model_free(sys_model);
        oval_results_model_free(res_model);

        /* FIN */
        if (result_false == 0) return 0;
        else return 2;
}
