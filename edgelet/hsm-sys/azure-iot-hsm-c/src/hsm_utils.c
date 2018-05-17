#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "azure_c_shared_utility/gballoc.h"
#include "azure_c_shared_utility/crt_abstractions.h"
#include "hsm_log.h"
#include "hsm_utils.h"

#define HSM_UTIL_SUCCESS 0
#define HSM_UTIL_ERROR 1
#define HSM_UTIL_EMPTY 2

#if defined __WINDOWS__ || defined _WIN32 || defined _WIN64 || defined _Windows
    #if !defined S_ISDIR
        #define S_ISDIR(m) (((m) & _S_IFDIR) == _S_IFDIR)
    #endif
#endif

static int read_file_into_buffer_impl
(
    const char* file_name,
    void  *output_buffer,
    size_t output_buffer_size,
    size_t *file_size_in_bytes
)
{
    FILE *file_handle;
    int result;
    long file_size;

    if (file_size_in_bytes != NULL)
    {
        *file_size_in_bytes = 0;
    }

    if ((file_handle = fopen(file_name, "r")) == NULL)
    {
        LOG_ERROR("Could not open file for reading %s", file_name);
        result = HSM_UTIL_ERROR;
    }
    else if (fseek(file_handle, 0, SEEK_END) != 0) // obtain file size by seek()'ing to the end
    {
        LOG_ERROR("fseek returned error for file %s", file_name);
        result = HSM_UTIL_ERROR;
    }
    else
    {
        file_size = ftell(file_handle);
        if (file_size < 0)
        {
            LOG_ERROR("ftell returned error for file %s", file_name);
            result = HSM_UTIL_ERROR;
        }
        else if (file_size == 0)
        {
            LOG_DEBUG("ftell returned zero bytes for file %s", file_name);
            result = HSM_UTIL_EMPTY;
        }
        else
        {
            result = HSM_UTIL_SUCCESS;
            if (file_size_in_bytes != NULL)
            {
                *file_size_in_bytes = file_size;
            }
            if (output_buffer != NULL)
            {
                size_t num_bytes_read;
                size_t num_bytes_to_read = (output_buffer_size < (size_t)file_size) ?
                                            output_buffer_size :
                                            (size_t)file_size;
                rewind(file_handle);
                num_bytes_read = fread(output_buffer, 1, file_size, file_handle);
                if (num_bytes_read != num_bytes_to_read)
                {
                    LOG_ERROR("File read failed for file %s", file_name);
                    result = HSM_UTIL_ERROR;
                }
            }
        }
    }

    if (file_handle != NULL)
    {
        fclose(file_handle);
    }

    return result;
}

static int write_ascii_buffer_into_file
(
    const char *file_name,
    const void *input_buffer,
    size_t input_buffer_size
)
{
    FILE *file_handle;
    int result;

    if ((file_handle = fopen(file_name, "w")) == NULL)
    {
        LOG_ERROR("Could not open file for writing %s", file_name);
        result = HSM_UTIL_ERROR;
    }
    else
    {
        size_t num_bytes_written;
        result = HSM_UTIL_SUCCESS;
        num_bytes_written = fwrite(input_buffer, 1, input_buffer_size, file_handle);
        if (num_bytes_written != input_buffer_size)
        {
            LOG_ERROR("File write failed for file %s", file_name);
            result = HSM_UTIL_ERROR;
        }
        (void)fclose(file_handle);
        if (result != HSM_UTIL_SUCCESS)
        {
            (void)delete_file(file_name);
        }
    }

    return result;
}

void* read_file_into_buffer(const char* file_name, size_t *output_buffer_size)
{
    void* result;
    size_t file_size_in_bytes = 0;

    if (output_buffer_size != NULL)
    {
        *output_buffer_size = 0;
    }

    if ((file_name == NULL) || (strlen(file_name) == 0))
    {
        LOG_ERROR("Invalid file name");
        result = NULL;
    }
    else if (read_file_into_buffer_impl(file_name, NULL, 0, &file_size_in_bytes) != HSM_UTIL_SUCCESS)
    {
        result = NULL;
    }
    else if ((result = malloc(file_size_in_bytes)) == NULL)
    {
        LOG_ERROR("Could not allocate memory to store the contents of the file %s", file_name);
    }
    else if (read_file_into_buffer_impl(file_name, result, file_size_in_bytes, NULL) != HSM_UTIL_SUCCESS)
    {
        LOG_ERROR("Could not read file into buffer");
        free(result);
        result = NULL;
    }
    else
    {
        if (output_buffer_size)
        {
            *output_buffer_size = file_size_in_bytes;
        }
    }

    return result;
}

char* read_file_into_cstring(const char* file_name, size_t *output_buffer_size)
{
    char* result;
    size_t file_size_in_bytes = 0;

    if (output_buffer_size != NULL)
    {
        *output_buffer_size = 0;
    }

    if ((file_name == NULL) || (strlen(file_name) == 0))
    {
        LOG_ERROR("Invalid file name");
        result = NULL;
    }
    else if (read_file_into_buffer_impl(file_name, NULL, 0, &file_size_in_bytes) != HSM_UTIL_SUCCESS)
    {
        result = NULL;
    }
    else
    {
        size_t file_size_in_bytes_check = file_size_in_bytes + 1;
        if (file_size_in_bytes_check < file_size_in_bytes) // check if roll over occured
        {
            LOG_ERROR("Unexpected file size for file %s", file_name);
            result = NULL;
        }
        else if ((result = (char*)malloc(file_size_in_bytes + 1)) == NULL)
        {
            LOG_ERROR("Could not allocate memory to store the contents of the file %s", file_name);
        }
        else if (read_file_into_buffer_impl(file_name, result, file_size_in_bytes, NULL) != HSM_UTIL_SUCCESS)
        {
            LOG_ERROR("Could not read file into buffer: %s", file_name);
            free(result);
            result = NULL;
        }
        else
        {
            result[file_size_in_bytes] = 0;
            if (output_buffer_size != NULL)
            {
                *output_buffer_size = file_size_in_bytes + 1;
            }
        }
    }
    return result;
}

char* concat_files_to_cstring(const char **file_names, int num_files)
{
    char *result;
    if ((file_names == NULL) || (num_files <= 0))
    {
        LOG_ERROR("Invalid parameters");
        result = NULL;
    }
    else
    {
        int index;
        bool errors_found = false;
        size_t accumulated_size = 0;
        size_t accumulated_size_check;
        for (index = 0; index < num_files; index++)
        {
            size_t buffer_size;
            int status = read_file_into_buffer_impl(file_names[index], NULL, 0, &buffer_size);
            if (status == HSM_UTIL_ERROR)
            {
                result = NULL;
                errors_found = true;
                break;
            }
            else
            {
                accumulated_size_check = accumulated_size + buffer_size;
                if (accumulated_size_check < accumulated_size)
                {
                    LOG_ERROR("Concatenated file sizes too large");
                    result = NULL;
                    errors_found = true;
                    break;
                }
                accumulated_size = accumulated_size_check;
            }
        }
        if (!errors_found)
        {
            // add one more for null term
            accumulated_size_check = accumulated_size + 1;
            if (accumulated_size_check < accumulated_size)
            {
                LOG_ERROR("Concatenated file sizes too large");
                result = NULL;
            }
            else
            {
                accumulated_size = accumulated_size_check;
                if ((result = (char*)malloc(accumulated_size)) == NULL)
                {
                    LOG_ERROR("Could not allocate memory to store the concatenated files");
                }
                else
                {
                    result[0] = 0;
                    for (index = 0; index < num_files; index++)
                    {
                        char *temp_result = read_file_into_cstring(file_names[index], NULL);
                        if (temp_result != NULL)
                        {
                            // todo optimize for inplace concat
                            (void)strncat(result, temp_result, accumulated_size);
                            free(temp_result);
                        }
                    }
                }
            }
        }
    }
    return result;
}

bool is_file_valid(const char* file_name)
{
    bool result = false;
    if (file_name != NULL)
    {
        FILE *file_handle;
        if ((file_handle = fopen(file_name, "r")) != NULL)
        {
            fclose(file_handle);
            result = true;
        }
    }
    return result;
}

bool is_directory_valid(const char* dir_path)
{
    bool result = false;
    if (dir_path != NULL)
    {
        struct stat info;
        if (stat(dir_path, &info) == 0)
        {
            if (S_ISDIR(info.st_mode))
            {
                result = true;
            }
        }
    }
    return result;
}

int write_cstring_to_file(const char* file_name, const char* data)
{
    int result;

    if ((file_name == NULL) || (strlen(file_name) == 0))
    {
        LOG_ERROR("Invalid file name parameter");
        result = __FAILURE__;
    }
    else if (data == NULL)
    {
        LOG_ERROR("Invalid data parameter");
        result = __FAILURE__;
    }
    else
    {
        result = write_ascii_buffer_into_file(file_name, data, strlen(data));
    }

    return result;
}

int delete_file(const char* file_name)
{
    int result;

    if ((file_name == NULL) || (strlen(file_name) == 0))
    {
        LOG_ERROR("Invalid file name");
        result = __FAILURE__;
    }
    else
    {
        result = remove(file_name);
    }

    return result;
}