/*
 * OCILIB - C Driver for Oracle (C Wrapper for Oracle OCI)
 *
 * Website: http://www.ocilib.net
 *
 * Copyright (c) 2007-2016 Vincent ROGIER <vince.rogier@ocilib.net>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ocilib_internal.h"

/* ********************************************************************************************* *
 *                             PRIVATE VARIABLES
 * ********************************************************************************************* */

static unsigned int ConversionModeValues[] = { OCI_DCM_DEFAULT, OCI_DCM_FORCE };

/* ********************************************************************************************* *
 *                             PRIVATE FUNCTIONS
 * ********************************************************************************************* */

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathSetArray
 * --------------------------------------------------------------------------------------------- */

boolean OCI_DirPathSetArray
(
    OCI_DirPath *dp,
    ub4 row_from
)
{
    OCI_CALL_DECLARE_CONTEXT(TRUE)

    boolean  res     = TRUE;
    ub1     *data    = NULL;
    ub4      size    = 0;
    ub1      flag    = 0;
    ub2      col     = 0;
    ub4      row     = 0;

    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    /* reset the number of entries et */

    dp->nb_entries = 0;

    /* set entries */

    for (row = row_from; (row < dp->nb_cur) && res; row++)
    {
        for (col = 0; (col < dp->nb_cols) && res; col++)
        {
            OCI_DirPathColumn *dpcol = &(dp->cols[col]);

            /* get internal data cell */

            data = ((ub1 *) dpcol->data) + (size_t) (row * dpcol->bufsize);
            size = dpcol->lens[row];
            flag = dpcol->flags[row];

            if (SQLT_NUM == dpcol->sqlcode)
            {
                OCINumber *num = (OCINumber *) data;

                data = &num->OCINumberPart[1];
            }

            /* set entry value */

            OCI_EXEC(OCIDirPathColArrayEntrySet(dp->arr, dp->con->err, (ub4) dp->nb_entries,
                                                   (ub2) (col), (ub1*) data, (ub4) size, flag))
        }

        /* increment number of item set */

        if (res)
        {
            dp->nb_entries++;
        }
    }

    return OCI_STATUS;
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPahArrayToStream
 * --------------------------------------------------------------------------------------------- */

unsigned int OCI_DirPathArrayToStream
(
    OCI_DirPath *dp,
    ub4 row_from
)
{
    OCI_CALL_DECLARE_CONTEXT(TRUE)

    unsigned int res = OCI_DPR_COMPLETE;
    sword        ret  = OCI_SUCCESS;

    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    /* convert the array to a stream */

    ret = OCIDirPathColArrayToStream(dp->arr, dp->ctx, dp->strm, dp->con->err, dp->nb_entries, (ub4) 0);

    switch (ret)
    {
        case OCI_SUCCESS:
        {
            res        = OCI_DPR_COMPLETE;
            dp->status = OCI_DPS_CONVERTED;
            break;
        }
        case OCI_ERROR:
        {
            res = OCI_DPR_ERROR;

            /* only raise the exception if we're not in force mode */

            if (OCI_DCM_DEFAULT == dp->cvt_mode)
            {
                OCI_ExceptionOCI(dp->con->err, dp->con, NULL, FALSE);
            }
            break;
        }
        case OCI_CONTINUE:
        {
            dp->status = OCI_DPS_CONVERTED;
            res        = OCI_DPR_FULL;
            break;
        }
        case OCI_NEED_DATA:
        {
            res = OCI_DPR_PARTIAL;
            break;
        }
    }

    if (OCI_FAILURE(ret))
    {
        ub4 err_row = 0;
        ub2 err_col = 0;
        ub4 size    = 0;

        size = sizeof(err_col);

        OCI_GET_ATTRIB(OCI_HTYPE_DIRPATH_COLUMN_ARRAY, OCI_ATTR_COL_COUNT, dp->arr, &err_col, &size)

        size = sizeof(err_row);

        OCI_GET_ATTRIB(OCI_HTYPE_DIRPATH_COLUMN_ARRAY, OCI_ATTR_ROW_COUNT, dp->arr, &err_row, &size)

        /* update converted rows so far */
        dp->nb_converted += err_row;

        /* record errors index on real error */
        if (OCI_DPR_ERROR == res)
        {
            dp->err_rows[dp->nb_err] = row_from + err_row;
            dp->err_cols[dp->nb_err] = err_col;

            dp->nb_err++;
        }
    }
    else
    {
        /* conversion is successful. the number of converted rows is the same
           as the number of row set*/
        dp->nb_converted += dp->nb_entries;
    }

    return res;
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPahArrayToStream
 * --------------------------------------------------------------------------------------------- */

unsigned int OCI_DirPathLoadStream(OCI_DirPath *dp)
{
    unsigned int res = OCI_DPR_COMPLETE;
    sword ret        = OCI_SUCCESS;
    ub4 nb_loaded    = 0;
    ub4 size         = sizeof(nb_loaded);

    /* load the stream */

    ret = OCIDirPathLoadStream(dp->ctx, dp->strm, dp->con->err);

    switch (ret)
    {
        case OCI_SUCCESS:
        {
            res         = OCI_DPR_COMPLETE;
            dp->status  = OCI_DPS_PREPARED;
            break;
        }
        case OCI_ERROR:
        {
            res = OCI_DPR_ERROR;

            OCI_ExceptionOCI(dp->con->err, dp->con, NULL, FALSE);
            break;
        }
        case OCI_NO_DATA:
        {
            res = OCI_DPR_EMPTY;
            break;
        }
        case OCI_NEED_DATA:
        {
            res = OCI_DPR_PARTIAL;
            break;
        }
    }

    /* retrieve the number of rows loaded so far */

    OCIAttrGet(dp->strm, OCI_HTYPE_DIRPATH_STREAM, &nb_loaded,
                &size, OCI_ATTR_ROW_COUNT, dp->con->err);

    dp->nb_loaded    += nb_loaded;
    dp->nb_processed += nb_loaded;

    /* On failure, record errors rows */

    if (OCI_FAILURE(ret))
    {
        dp->err_rows[dp->nb_err] = (dp->nb_err > 0) ? (dp->err_rows[dp->nb_err-1] + nb_loaded + 1) : dp->nb_loaded;
        dp->err_cols[dp->nb_err] = 0;
        dp->nb_err++;
    }

    return res;
}

/* ********************************************************************************************* *
 *                            PUBLIC FUNCTIONS
 * ********************************************************************************************* */

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathCreate
 * --------------------------------------------------------------------------------------------- */

OCI_DirPath * OCI_API OCI_DirPathCreate
(
    OCI_TypeInfo *typinf,
    const otext  *partition,
    unsigned int  nb_cols,
    unsigned int  nb_rows
)
{
    OCI_DirPath *dp = NULL;

    OCI_CALL_ENTER(OCI_DirPath*, dp)
    OCI_CALL_CHECK_PTR(OCI_IPC_TYPE_INFO, typinf)
    OCI_CALL_CHECK_COMPAT(typinf->con, typinf->type != OCI_TIF_TYPE)
    OCI_CALL_CHECK_BOUND(typinf->con, nb_cols, 1, typinf->nb_cols)
    OCI_CALL_CONTEXT_SET(typinf->con, NULL, typinf->con->err)

    /* allocate direct path structure */

    dp = (OCI_DirPath *)OCI_MemAlloc(OCI_IPC_DIRPATH, sizeof(*dp), (size_t)1, TRUE);

    if (dp)
    {
        dbtext *dbstr  = NULL;
        int     dbsize = -1;

        dp->con        = typinf->con;
        dp->status     = OCI_DPS_NOT_PREPARED;
        dp->cvt_mode   = OCI_DCM_DEFAULT;
        dp->res_conv   = OCI_DPR_EMPTY;
        dp->res_load   = OCI_DPR_EMPTY;
        dp->typinf     = typinf;
        dp->nb_rows    = (ub2)nb_rows;
        dp->nb_cols    = (ub2)nb_cols;
        dp->nb_cur     = (ub2)dp->nb_rows;

        /* allocates direct context handle */

        OCI_STATUS = OCI_HandleAlloc((dvoid *)dp->con->env, (dvoid **) (void *) &dp->ctx, OCI_HTYPE_DIRPATH_CTX);

        /* set table name attribute */

        if (OCI_STATUS)
        {
            dbsize = -1;
            dbstr  = OCI_StringGetOracleString(dp->typinf->name, &dbsize);

            OCI_SET_ATTRIB(OCI_HTYPE_DIRPATH_CTX, OCI_ATTR_NAME, dp->ctx, dbstr, dbsize)

            OCI_StringReleaseOracleString(dbstr);
        }

        /* set schema name attribute */

        if (OCI_STATUS && dp->typinf->schema && dp->typinf->schema[0])
        {
            dbsize = -1;
            dbstr  = OCI_StringGetOracleString(dp->typinf->schema, &dbsize);

            OCI_SET_ATTRIB(OCI_HTYPE_DIRPATH_CTX, OCI_ATTR_SCHEMA_NAME, dp->ctx, dbstr, dbsize)

            OCI_StringReleaseOracleString(dbstr);
        }

        /* set partition name attribute */

        if (OCI_STATUS && partition && partition[0])
        {
            dbsize = -1;
            dbstr  = OCI_StringGetOracleString(partition, &dbsize);

            OCI_SET_ATTRIB(OCI_HTYPE_DIRPATH_CTX, OCI_ATTR_SUB_NAME, dp->ctx, dbstr, dbsize)

            OCI_StringReleaseOracleString(dbstr);
        }

        if (OCILib.version_runtime >= OCI_9_0)
        {
            ub4 num_rows = dp->nb_rows;

            /* set array size attribute */

            OCI_SET_ATTRIB(OCI_HTYPE_DIRPATH_CTX, OCI_ATTR_NUM_ROWS, dp->ctx, &num_rows, sizeof(num_rows))
        }

        /* set columns count attribute */

        OCI_SET_ATTRIB(OCI_HTYPE_DIRPATH_CTX, OCI_ATTR_NUM_COLS, dp->ctx, &dp->nb_cols, sizeof(dp->nb_cols))

        /* allocating the column array */

        if (OCI_STATUS)
        {
            dp->cols = (OCI_DirPathColumn *) OCI_MemAlloc(OCI_IPC_DP_COL_ARRAY, sizeof(OCI_DirPathColumn),
                                                          (size_t) dp->nb_cols, TRUE);

            OCI_STATUS = (NULL != dp->cols);
        }
    }

    /* handle errors */

    if (OCI_STATUS)
    {
        OCI_RETVAL = dp;
    }
    else if (dp)
    {
        OCI_DirPathFree(dp);
    }

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathFree
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_DirPathFree
(
    OCI_DirPath *dp
)
{
    ub2 i = 0;

    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    for (i = 0; i < dp->nb_cols; i++)
    {
        OCI_FREE(dp->cols[i].data)
        OCI_FREE(dp->cols[i].lens)
        OCI_FREE(dp->cols[i].flags)
        OCI_FREE(dp->cols[i].format)
    }

    OCI_FREE(dp->cols)
    OCI_FREE(dp->err_cols)
    OCI_FREE(dp->err_rows)

    OCI_HandleFree(dp->strm, OCI_HTYPE_DIRPATH_STREAM);
    OCI_HandleFree(dp->arr,  OCI_HTYPE_DIRPATH_COLUMN_ARRAY);
    OCI_HandleFree(dp->ctx,  OCI_HTYPE_DIRPATH_CTX);

    OCI_FREE(dp)

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathSetColumn
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_DirPathSetColumn
(
    OCI_DirPath *dp,
    unsigned int index,
    const otext *name,
    unsigned int maxsize,
    const otext *format
)
{
    OCI_DirPathColumn *dpcol  = NULL;
    OCI_Column        *col    = NULL;
    OCIParam          *hattr  = NULL;
    OCIParam          *hlist  = NULL;
    dbtext            *dbstr  = NULL;
    int                dbsize = -1;

    ub2 i = 0;

    OCI_CALL_ENTER(boolean, FALSE)

    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CHECK_DIRPATH_STATUS(dp, OCI_DPS_NOT_PREPARED)
    OCI_CALL_CHECK_PTR(OCI_IPC_STRING, name)
    OCI_CALL_CHECK_BOUND(dp->con, index, 1, dp->nb_cols)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    /* check if column exists */

    for (i = 0; i < dp->typinf->nb_cols; i++)
    {
        if (0 == ostrcasecmp(name, dp->typinf->cols[i].name))
        {
            break;
        }
    }

    /* check if column was found */

    if (i >= dp->typinf->nb_cols)
    {
        OCI_RAISE_EXCEPTION(OCI_ExceptionDirPathColNotFound(dp, name, dp->typinf->name))
    }

    /* set column information */

    if (OCI_STATUS)
    {
        col   = &dp->typinf->cols[i];
        dpcol = &dp->cols[index-1];

        /* default column attributes */

        dpcol->maxsize     = (ub2) maxsize;
        dpcol->bufsize     = (ub2) maxsize + 1;
        dpcol->sqlcode     = SQLT_CHR;
        dpcol->type        = OCI_DDT_TEXT;
        dpcol->index       = i;
        dpcol->format_size = 0;

        switch (col->datatype)
        {
            case OCI_CDT_TEXT:
            {
                dpcol->maxsize *= sizeof(otext);
                dpcol->bufsize *= sizeof(otext);

                if (OCILib.nls_utf8)
                {
                    dpcol->bufsize *= UTF8_BYTES_PER_CHAR;
                }
                break;
            }
            case OCI_CDT_NUMERIC:
            {
                if (format && format[0])
                {
                    dpcol->format      = ostrdup(format);
                    dpcol->format_size = (ub4) ostrlen(format);
                    dpcol->type        = OCI_DDT_NUMBER;
                    dpcol->sqlcode     = SQLT_NUM;
                    dpcol->bufsize     = sizeof(OCINumber);
                    dpcol->maxsize     = sizeof(OCINumber);
                }
                else
                {
                    dpcol->type = OCI_DDT_OTHERS;
                }
                break;
            }
            case OCI_CDT_DATETIME:
            case OCI_CDT_TIMESTAMP:
            case OCI_CDT_INTERVAL:
            {
                dpcol->type = OCI_DDT_OTHERS;

                if (format && format[0])
                {
                    dpcol->format      = ostrdup(format);
                    dpcol->format_size = (ub4) ostrlen(format);
                    dpcol->maxsize     = (ub2) dpcol->format_size;
                    dpcol->bufsize    *= sizeof(otext);
                }
                break;
            }
            case OCI_CDT_LOB:
            {
                if (OCI_BLOB == col->subtype)
                {
                    dpcol->type    = OCI_DDT_BINARY;
                    dpcol->sqlcode = SQLT_BIN;
                }

                break;
            }
            case OCI_CDT_LONG:
            {
                if (OCI_BLONG == col->subtype)
                {
                    dpcol->type    = OCI_DDT_BINARY;
                    dpcol->sqlcode = SQLT_BIN;
                }
                break;
            }
            case OCI_CDT_RAW:
            {
                dpcol->type    = OCI_DDT_BINARY;
                dpcol->sqlcode = SQLT_BIN;
                break;
            }
            default:
            {
                OCI_STATUS = FALSE;
                OCI_ExceptionDatatypeNotSupported(dp->con, NULL, col->libcode);
                break;
            }
        }
    }

    /* if supported data type, set direct path column attributes */

    if (OCI_STATUS)
    {
        /* get column parameter list handle */

        OCI_GET_ATTRIB(OCI_HTYPE_DIRPATH_CTX, OCI_ATTR_LIST_COLUMNS, dp->ctx, &hlist, NULL)

        /* get column attribute handle */

        OCI_EXEC(OCIParamGet((dvoid *) hlist, OCI_DTYPE_PARAM, dp->con->err, (dvoid** ) (dvoid *) &hattr, (ub4) index))

        /* set column name */

        if (OCI_STATUS)
        {
            dbsize = -1;
            dbstr  = OCI_StringGetOracleString(name, &dbsize);

            OCI_SET_ATTRIB(OCI_DTYPE_PARAM, OCI_ATTR_NAME, hattr, dbstr, dbsize)

            OCI_StringReleaseOracleString(dbstr);
        }

        /* set column type */

        OCI_SET_ATTRIB(OCI_DTYPE_PARAM, OCI_ATTR_DATA_TYPE, hattr, &dpcol->sqlcode, sizeof(dpcol->sqlcode))

        /* set column size */

        OCI_SET_ATTRIB(OCI_DTYPE_PARAM, OCI_ATTR_DATA_SIZE, hattr, &dpcol->maxsize, sizeof(dpcol->maxsize))

        /* set column precision */

        if (col->prec != 0)
        {
            OCI_SET_ATTRIB(OCI_DTYPE_PARAM, OCI_ATTR_PRECISION, hattr, &col->prec, sizeof(col->prec))
        }

        /* set column scale */

        if (col->scale != 0)
        {
            OCI_SET_ATTRIB(OCI_DTYPE_PARAM, OCI_ATTR_SCALE, hattr, &col->scale, sizeof(col->scale))
        }

        /* set column date/time format attribute */

        if (OCI_STATUS && dpcol->format && dpcol->format[0] && (OCI_DDT_NUMBER != dpcol->type))
        {
            dbsize = -1;
            dbstr  = OCI_StringGetOracleString(dpcol->format, &dbsize);

            OCI_SET_ATTRIB(OCI_DTYPE_PARAM, OCI_ATTR_DATEFORMAT, hattr, dbstr, dbsize)

            OCI_StringReleaseOracleString(dbstr);
        }

        /* setup Unicode mode for Unicode user data */

        if (OCI_DDT_TEXT == dpcol->type && OCI_CHAR_WIDE == OCILib.charset)
        {
            ub2 csid = OCI_UTF16ID;

            OCI_SET_ATTRIB(OCI_DTYPE_PARAM, OCI_ATTR_CHARSET_ID, hattr, &csid, sizeof(csid))
        }

        /* free param handle */

        OCIDescriptorFree(hattr, OCI_DTYPE_PARAM);
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathPrepare
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_DirPathPrepare
(
    OCI_DirPath *dp
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CHECK_DIRPATH_STATUS(dp, OCI_DPS_NOT_PREPARED)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    /* prepare direct path operation */

    OCI_EXEC(OCIDirPathPrepare(dp->ctx, dp->con->cxt, dp->con->err))

    /* allocate column array handle */

    OCI_STATUS = OCI_STATUS &&  OCI_HandleAlloc((dvoid *)dp->ctx, (dvoid **)(void *)&dp->arr, OCI_HTYPE_DIRPATH_COLUMN_ARRAY);

    /* allocate stream handle */

    OCI_STATUS = OCI_STATUS && OCI_HandleAlloc((dvoid *)dp->ctx, (dvoid **)(void *)&dp->strm, OCI_HTYPE_DIRPATH_STREAM);

    /* check the number of rows allocated */

    if (OCI_STATUS)
    {
        ub4 num_rows = 0;
        ub4 size     = sizeof(num_rows);

        OCI_GET_ATTRIB(OCI_HTYPE_DIRPATH_COLUMN_ARRAY, OCI_ATTR_NUM_ROWS, dp->arr, &num_rows, &size)

        dp->nb_cur  = (ub2) num_rows;
        dp->nb_rows = (ub2) num_rows;
    }

    /* allocate array of errs rows */

    if (OCI_STATUS)
    {
        dp->err_rows = (ub4 *) OCI_MemAlloc(OCI_IPC_BUFF_ARRAY, sizeof(*dp->err_rows), (size_t) dp->nb_cur, TRUE);

        OCI_STATUS = (NULL != dp->err_rows);
    }

    /* allocate array of errs cols */

    if (OCI_STATUS)
    {
        dp->err_cols = (ub2 *) OCI_MemAlloc(OCI_IPC_BUFF_ARRAY, sizeof(*dp->err_cols),
                                            (size_t) dp->nb_cur, TRUE);

        OCI_STATUS = (NULL != dp->err_cols);
    }

    /* now, we need to allocate internal buffers */

    if (OCI_STATUS)
    {
        ub2 i;

        for (i = 0; i < dp->nb_cols; i++)
        {
            OCI_DirPathColumn *col = &dp->cols[i];

            /* data buffers */

            col->data = (ub1 *) OCI_MemAlloc(OCI_IPC_BUFF_ARRAY, (size_t) col->bufsize, (size_t) dp->nb_cur, TRUE);

            if (!col->data)
            {
                OCI_STATUS = FALSE;
                break;
            }

            /* data sizes */

            col->lens = (ub4 *) OCI_MemAlloc(OCI_IPC_BUFF_ARRAY, sizeof(ub4),
                                             (size_t) dp->nb_cur, TRUE);

            if (!col->lens)
            {
                OCI_STATUS = FALSE;
                break;
            }

            /* data flags */

            col->flags = (ub1 *) OCI_MemAlloc(OCI_IPC_BUFF_ARRAY, sizeof(ub1), (size_t) dp->nb_cur, TRUE);

            if (!col->flags)
            {
                OCI_STATUS = FALSE;
                break;
            }
        }
    }

    if (OCI_STATUS)
    {
        dp->status = OCI_DPS_PREPARED;
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathSetEntry
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_DirPathSetEntry
(
    OCI_DirPath *dp,
    unsigned int row,
    unsigned int index,
    void        *value,
    unsigned int size,
    boolean      complete
)
{
    OCI_DirPathColumn *dpcol = NULL;

    ub1 *data;
    ub1 flag;

    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CHECK_DIRPATH_STATUS(dp, OCI_DPS_PREPARED)
    OCI_CALL_CHECK_BOUND(dp->con, index, 1, dp->nb_cols)
    OCI_CALL_CHECK_BOUND(dp->con, row, 1, dp->nb_cur)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    dpcol = &dp->cols[index-1];

    if (dpcol)
    {
        OCI_STATUS = TRUE;

        /* check size */

        if (size > dpcol->maxsize)
        {
            size = (unsigned int) dpcol->maxsize;
        }

        /* setup column flag */

        if (!value)
        {
            flag = OCI_DIRPATH_COL_NULL;
            size = 0;
        }
        else if (complete)
        {
            flag = OCI_DIRPATH_COL_COMPLETE;
        }
        else
        {
            flag = OCI_DIRPATH_COL_PARTIAL;
        }

        /* Process only if data is not null */

        if (value)
        {
            /* for character based column, parameter size was the number of characters */

            if (SQLT_CHR == dpcol->sqlcode)
            {
                size *= (unsigned int) sizeof(otext);
            }

            /* get internal data cell */

            data = ((ub1 *) dpcol->data) + (size_t) ((row-1) * dpcol->bufsize);

            /* we weed to pack the buffer if wchar_t is 4 bytes */

            if (OCI_DDT_TEXT == dpcol->type && OCILib.use_wide_char_conv)
            {
                size = ocharcount(size);
                OCI_StringUTF32ToUTF16(value, data, size);
            }
            else if (OCI_DDT_OTHERS == dpcol->type && OCI_CHAR_WIDE == OCILib.charset)
            {
                /* input Unicode numeric values causes oracle conversion error.
                   so, let's convert them to ANSI */

                size = ocharcount(size);
                OCI_StringNativeToAnsi(value, data, size);
            }
            else if (OCI_DDT_NUMBER == dpcol->type)
            {
               /* if a format was provided for a numeric column, we convert the input
               buffer to a OCINumber */

                OCINumber *num = (OCINumber *) data;

                OCI_STATUS = OCI_NumberFromString(dp->con, num, sizeof(*num), OCI_NUM_NUMBER,
                                                   SQLT_VNU, (dtext *)value, dpcol->format);

                if (OCI_STATUS)
                {
                    size = (unsigned int) num->OCINumberPart[0];
                }
            }
            else
            {
                memcpy(data, value, (size_t) size);
            }
        }

        dpcol->lens[row-1]  = size;
        dpcol->flags[row-1] = flag;
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathReset
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_DirPathReset
(
    OCI_DirPath *dp
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    /* reset conversion and loading variables */

    dp->nb_processed    = 0;
    dp->nb_converted    = 0;
    dp->nb_err          = 0;
    dp->idx_err_row     = 0;
    dp->idx_err_col     = 0;

    /* reset array */

    OCI_EXEC(OCIDirPathColArrayReset(dp->arr, dp->con->err))

    /* reset stream */

    OCI_EXEC(OCIDirPathStreamReset(dp->strm, dp->con->err))

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathConvert
 * --------------------------------------------------------------------------------------------- */

unsigned int OCI_API OCI_DirPathConvert
(
    OCI_DirPath *dp
)
{
    ub4 row_from = 0;

    OCI_CALL_ENTER(unsigned int, OCI_DPR_ERROR)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CHECK_DIRPATH_STATUS(dp, OCI_DPS_PREPARED)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    /* reset the number of processed rows */

    dp->nb_processed = 0;

    /* in case of previous error in default mode or if the stream is full,
       let's start again from the last faulted row */

    if ((OCI_DCM_DEFAULT == dp->cvt_mode || OCI_DPR_FULL == dp->res_conv) && (dp->nb_err > 0))
    {
        row_from = dp->err_rows[dp->nb_err - 1];
    }

    /* reset the stream if it is full */

    if (OCI_DPR_FULL == dp->res_conv)
    {
        OCI_EXEC(OCIDirPathStreamReset(dp->strm, dp->con->err))
    }

    /* reset conversion status back to default error value */

    dp->res_conv = OCI_DPR_ERROR;

    /* set array values */

    if (OCI_STATUS && OCI_DirPathSetArray(dp, row_from))
    {
        /* try to convert values from array into stream */

        dp->res_conv = OCI_DirPathArrayToStream(dp, row_from);

        /* in case of conversion error, continue conversion in force mode
           other return from conversion */

        if (OCI_DCM_FORCE == dp->cvt_mode && OCI_DPR_ERROR == dp->res_conv)
        {
            /* perform conversion until all non erred rows are converted */

            while (OCI_STATUS && (OCI_DPR_ERROR == dp->res_conv) && (dp->nb_err <= dp->nb_cur))
            {
                /* start from the row that follows the last erred row */

                row_from = dp->err_rows[dp->nb_err - 1] + 1;

                /* set values again */

                OCI_STATUS = OCI_DirPathSetArray(dp, row_from);

                if (OCI_STATUS)
                {
                     /* perform conversion again */

                     dp->res_conv = OCI_DirPathArrayToStream(dp, row_from);
                }
            }
        }
    }

    dp->nb_processed = dp->nb_converted;

    OCI_STATUS = OCI_STATUS && (OCI_DPR_COMPLETE == dp->res_conv);
    OCI_RETVAL = dp->res_conv;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathLoad
 * --------------------------------------------------------------------------------------------- */

unsigned int OCI_API OCI_DirPathLoad
(
    OCI_DirPath *dp
)
{
    OCI_CALL_ENTER(unsigned int, OCI_DPR_ERROR)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CHECK_DIRPATH_STATUS(dp, OCI_DPS_CONVERTED)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    /* reset the number of processed rows */

    dp->nb_processed = 0;

   /* reset errors variables as OCI_DirPathLoad() is not reentrant */

    dp->nb_err       = 0;
    dp->idx_err_col  = 0;
    dp->idx_err_row  = 0;
    dp->res_load     = OCI_DPR_COMPLETE;

    /* load the stream */

    dp->res_load = OCI_DirPathLoadStream(dp);

    /* continue to load the stream while it returns an error */

    while (OCI_DPR_ERROR == dp->res_load)
    {
        dp->res_load = OCI_DirPathLoadStream(dp);
    }

    OCI_STATUS = OCI_STATUS && (OCI_DPR_COMPLETE == dp->res_load);
    OCI_RETVAL = dp->res_load;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathFinish
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_DirPathFinish
(
    OCI_DirPath *dp
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CHECK_DIRPATH_STATUS(dp, OCI_DPS_PREPARED)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    OCI_EXEC(OCIDirPathFinish(dp->ctx, dp->con->err))

    if (OCI_STATUS)
    {
        dp->status = OCI_DPS_TERMINATED;
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathAbort
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_DirPathAbort
(
    OCI_DirPath *dp
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CHECK_DIRPATH_STATUS(dp, OCI_DPS_PREPARED)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    OCI_EXEC(OCIDirPathAbort(dp->ctx, dp->con->err))

    if (OCI_STATUS)
    {
        dp->status = OCI_DPS_TERMINATED;
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathSave
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_DirPathSave
(
    OCI_DirPath *dp
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CHECK_DIRPATH_STATUS(dp, OCI_DPS_PREPARED)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    OCI_EXEC(OCIDirPathDataSave(dp->ctx, dp->con->err, OCI_DIRPATH_DATASAVE_SAVEONLY))

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathFlushRow
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_DirPathFlushRow
(
    OCI_DirPath *dp
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CHECK_DIRPATH_STATUS(dp, OCI_DPS_PREPARED)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    OCI_EXEC(OCIDirPathFlushRow(dp->ctx, dp->con->err))

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathSetCurrentRows
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_DirPathSetCurrentRows
(
    OCI_DirPath *dp,
    unsigned int nb_rows
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CHECK_DIRPATH_STATUS(dp, OCI_DPS_PREPARED)
    OCI_CALL_CHECK_BOUND(dp->con, nb_rows, 1, dp->nb_rows)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    dp->nb_cur = (ub2) nb_rows;

    OCI_RETVAL = OCI_STATUS = TRUE;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathGetCurrentRows
 * --------------------------------------------------------------------------------------------- */

unsigned int OCI_API OCI_DirPathGetCurrentRows
(
    OCI_DirPath *dp
)
{
    OCI_GET_PROP(unsigned int, 0, OCI_IPC_DIRPATH, dp, nb_cur, dp->con, NULL, dp->con->err)
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathGetMaxRows
 * --------------------------------------------------------------------------------------------- */

unsigned int OCI_API OCI_DirPathGetMaxRows
(
    OCI_DirPath *dp
)
{
   OCI_GET_PROP(unsigned int, 0, OCI_IPC_DIRPATH, dp, nb_rows, dp->con, NULL, dp->con->err)
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathSetDateFormat
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_DirPathSetDateFormat
(
    OCI_DirPath *dp,
    const otext *format
)
{
    dbtext  *dbstr = NULL;
    int     dbsize = -1;

    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CHECK_DIRPATH_STATUS(dp, OCI_DPS_NOT_PREPARED)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    dbsize = -1;
    dbstr  = OCI_StringGetOracleString(format, &dbsize);

    OCI_SET_ATTRIB(OCI_HTYPE_DIRPATH_CTX, OCI_ATTR_DATEFORMAT, dp->ctx, dbstr, dbsize)

    OCI_StringReleaseOracleString(dbstr);

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathSetParallel
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_DirPathSetParallel
(
    OCI_DirPath *dp,
    boolean      value
)
{
    ub1 enabled = (ub1) value;

    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CHECK_DIRPATH_STATUS(dp, OCI_DPS_NOT_PREPARED)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    OCI_SET_ATTRIB(OCI_HTYPE_DIRPATH_CTX, OCI_ATTR_DIRPATH_PARALLEL, dp->ctx, &enabled, sizeof(enabled))

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathSetNoLog
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_DirPathSetNoLog
(
    OCI_DirPath *dp,
    boolean      value
)
{
    ub1 nolog = (ub1) value;

    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CHECK_DIRPATH_STATUS(dp, OCI_DPS_NOT_PREPARED)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    OCI_SET_ATTRIB(OCI_HTYPE_DIRPATH_CTX, OCI_ATTR_DIRPATH_NOLOG, dp->ctx, &nolog, sizeof(nolog))

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathSetCacheSize
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_DirPathSetCacheSize
(
    OCI_DirPath *dp,
    unsigned int size
)
{
    ub4 cache_size  = size;
    boolean enabled = FALSE;

    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CHECK_DIRPATH_STATUS(dp, OCI_DPS_NOT_PREPARED)
    OCI_CALL_CHECK_DIRPATH_STATUS(dp, OCI_DPS_NOT_PREPARED)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

#if OCI_VERSION_COMPILE >= OCI_9_2

    OCI_SET_ATTRIB(OCI_HTYPE_DIRPATH_CTX, OCI_ATTR_DIRPATH_DCACHE_SIZE, dp->ctx, &cache_size, sizeof(cache_size))
    OCI_SET_ATTRIB(OCI_HTYPE_DIRPATH_CTX, OCI_ATTR_DIRPATH_DCACHE_DISABLE, dp->ctx, &enabled, sizeof(enabled))

#else

    OCI_NOT_USED(cache_size)
    OCI_NOT_USED(enabled)

#endif

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathSetBufferSize
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_DirPathSetBufferSize
(
    OCI_DirPath *dp,
    unsigned int size
)
{
    ub4 bufsize = (ub4) size;

    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CHECK_DIRPATH_STATUS(dp, OCI_DPS_NOT_PREPARED)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    OCI_SET_ATTRIB(OCI_HTYPE_DIRPATH_CTX, OCI_ATTR_BUF_SIZE, dp->ctx, &bufsize, sizeof(bufsize))

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathSetConvertMode
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_DirPathSetConvertMode
(
    OCI_DirPath *dp,
    unsigned int mode
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CHECK_DIRPATH_STATUS(dp, OCI_DPS_NOT_PREPARED)
    OCI_CALL_CHECK_ENUM_VALUE(dp->con, NULL, mode, ConversionModeValues, OTEXT("Conversion mode"))
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    dp->cvt_mode = (ub2)mode;

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathGetRowCount
 * --------------------------------------------------------------------------------------------- */

unsigned int OCI_API OCI_DirPathGetRowCount
(
    OCI_DirPath *dp
)
{
    OCI_GET_PROP(unsigned int, 0, OCI_IPC_DIRPATH, dp, nb_loaded, dp->con, NULL, dp->con->err)
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathGetAffectedRows
 * --------------------------------------------------------------------------------------------- */

unsigned int OCI_API OCI_DirPathGetAffectedRows
(
    OCI_DirPath *dp
)
{
    OCI_GET_PROP(unsigned int, 0, OCI_IPC_DIRPATH, dp, nb_processed, dp->con, NULL, dp->con->err)
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathGetErrorColumn
 * --------------------------------------------------------------------------------------------- */

unsigned int OCI_API OCI_DirPathGetErrorColumn
(
    OCI_DirPath *dp
)
{
    OCI_CALL_ENTER(unsigned int, 0)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    if (dp->idx_err_col < dp->nb_err)
    {
        OCI_RETVAL = (unsigned int) dp->err_cols[dp->idx_err_col++] + 1;
    }

    OCI_STATUS = TRUE;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DirPathGetErrorRow
 * --------------------------------------------------------------------------------------------- */

unsigned int OCI_API OCI_DirPathGetErrorRow
(
    OCI_DirPath *dp
)
{
    OCI_CALL_ENTER(unsigned int, 0)
    OCI_CALL_CHECK_PTR(OCI_IPC_DIRPATH, dp)
    OCI_CALL_CONTEXT_SET(dp->con, NULL, dp->con->err)

    if (dp->idx_err_row < dp->nb_err)
    {
        OCI_RETVAL = (unsigned int) dp->err_rows[dp->idx_err_row++] + 1;
    }

    OCI_STATUS = TRUE;

    OCI_CALL_EXIT()
}
