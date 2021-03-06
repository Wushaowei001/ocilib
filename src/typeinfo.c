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

static unsigned int TypeInfoTypeValues[] = { OCI_TIF_TABLE, OCI_TIF_VIEW, OCI_TIF_TYPE };

/* ********************************************************************************************* *
 *                             PRIVATE FUNCTIONS
 * ********************************************************************************************* */

/* --------------------------------------------------------------------------------------------- *
 * OCI_TypeInfoClose
 * --------------------------------------------------------------------------------------------- */

boolean OCI_TypeInfoClose
(
    OCI_TypeInfo *typinf
)
{
    ub2 i;

    OCI_CHECK(NULL == typinf, FALSE);

    for (i=0; i < typinf->nb_cols; i++)
    {
        OCI_FREE(typinf->cols[i].name)
    }

    OCI_FREE(typinf->cols)
    OCI_FREE(typinf->name)
    OCI_FREE(typinf->schema)
    OCI_FREE(typinf->offsets)

    return TRUE;
}

/* ********************************************************************************************* *
 *                            PUBLIC FUNCTIONS
 * ********************************************************************************************* */

/* --------------------------------------------------------------------------------------------- *
 * OCI_TypeInfoGet
 * --------------------------------------------------------------------------------------------- */

OCI_TypeInfo * OCI_API OCI_TypeInfoGet
(
    OCI_Connection *con,
    const otext    *name,
    unsigned int    type
)
{
    OCI_TypeInfo *typinf        = NULL;
    OCI_TypeInfo *syn_typinf    = NULL;
    OCI_Item *item              = NULL;
    OCIDescribe *dschp          = NULL;
    OCIParam *param_root        = NULL;
    OCIParam *param_type        = NULL;
    OCIParam *param_cols        = NULL;
    OCIParam *param_list        = NULL;
    otext *str                  = NULL;
    int ptype                   = 0;
    ub1 desc_type               = 0;
    ub4 attr_type               = 0;
    ub4 num_type                = 0;
    boolean found               = FALSE;
    ub2 i;

    otext obj_schema[OCI_SIZE_OBJ_NAME + 1];
    otext obj_name[OCI_SIZE_OBJ_NAME + 1];

    OCI_CALL_ENTER(OCI_TypeInfo*, NULL)
    OCI_CALL_CHECK_PTR(OCI_IPC_CONNECTION, con)
    OCI_CALL_CHECK_PTR(OCI_IPC_STRING, name)
    OCI_CALL_CHECK_ENUM_VALUE(con, NULL, type, TypeInfoTypeValues, OTEXT("Type"))
    OCI_CALL_CONTEXT_SET(con, NULL, con->err)

    obj_schema[0] = 0;
    obj_name[0]   = 0;

    /* is the schema provided in the object name ? */

    for (str = (otext *) name; *str; str++)
    {
        if (*str == OTEXT('.'))
        {
            ostrncat(obj_schema, name, str-name);
            ostrncat(obj_name, ++str, (size_t) OCI_SIZE_OBJ_NAME);
            break;
        }
    }

    /* if the schema is not provided, we just copy the object name */

    if (!obj_name[0])
    {
        ostrncat(obj_name, name, (size_t) OCI_SIZE_OBJ_NAME);
    }

    /* type name must be uppercase if not quoted */

    if (obj_name[0] != OTEXT('"'))
    {
        for (str = obj_name; *str; str++)
        {
            *str = (otext)otoupper(*str);
        }
    }

    /* schema name must be uppercase if not quoted */

    if (obj_schema[0] != OTEXT('"'))
    {
        for (str = obj_schema; *str; str++)
        {
            *str = (otext)otoupper(*str);
        }
    }    

    /* first try to find it in list */

    item = con->tinfs->head;

    /* walk along the list to find the type */

    while (item)
    {
        typinf = (OCI_TypeInfo *) item->data;

        if (typinf && (typinf->type == type))
        {
            if ((ostrcasecmp(typinf->name,   obj_name  ) == 0) &&
                (ostrcasecmp(typinf->schema, obj_schema) == 0))
            {
                found = TRUE;
                break;
            }
        }

        item = item->next;
    }

    /* Not found, so create type object */

    if (!found)
    {
        item = OCI_ListAppend(con->tinfs, sizeof(OCI_TypeInfo));

        OCI_STATUS = (NULL != item);

        /* allocate describe handle */

        if (OCI_STATUS)
        {
            typinf = (OCI_TypeInfo *) item->data;

            typinf->con         = con;
            typinf->name        = ostrdup(obj_name);
            typinf->schema      = ostrdup(obj_schema);
            typinf->struct_size = 0;
            typinf->align       = 0;

            OCI_STATUS = OCI_HandleAlloc(typinf->con->env, (dvoid **)(void *)&dschp, OCI_HTYPE_DESCRIBE);
        }

        /* perform describe */

        if (OCI_STATUS)
        {
            otext buffer[(OCI_SIZE_OBJ_NAME * 2) + 2] = OTEXT("");

            size_t  max_chars = sizeof(buffer) / sizeof(otext) - 1;
            dbtext *dbstr1  = NULL;
            int     dbsize1 = -1;
            sb4     pbsp    = 1;

            str = buffer;

            /* compute full object name */

            if (typinf->schema && typinf->schema[0])
            {
                str = ostrncat(buffer, typinf->schema, max_chars);
                max_chars -= ostrlen(typinf->schema);

                str = ostrncat(str, OTEXT("."), max_chars);
                max_chars -= (size_t)1;
            }

            ostrncat(str, typinf->name, max_chars);

            dbstr1 = OCI_StringGetOracleString(str, &dbsize1);

            /* set public scope to include synonyms */
                
            OCI_SET_ATTRIB(OCI_HTYPE_DESCRIBE, OCI_ATTR_DESC_PUBLIC, dschp, &pbsp, sizeof(pbsp))
 
            /* describe call */

            OCI_EXEC
            (
                OCIDescribeAny(con->cxt, con->err, (dvoid *) dbstr1,
                               (ub4) dbsize1, OCI_OTYPE_NAME,
                               OCI_DEFAULT, OCI_PTYPE_UNK, dschp)
            )

            OCI_StringReleaseOracleString(dbstr1);

            /* get parameter handle */
                
            OCI_GET_ATTRIB(OCI_HTYPE_DESCRIBE, OCI_ATTR_PARAM, dschp, &param_root, NULL)
          
            /* get describe type */

            OCI_GET_ATTRIB(OCI_DTYPE_PARAM, OCI_ATTR_PTYPE, param_root, &desc_type, NULL)
        }

        /* on successful describe call, retrieve all information about the object 
           if it is not a synonym */

        if (OCI_STATUS)
        {
            switch (desc_type)
            {
                case OCI_PTYPE_TYPE:
                case OCI_PTYPE_LIST:
                {
                    boolean pdt = FALSE;
                    OCIRef *ref = NULL;
                    
                    OCI_STATUS = (OCI_TIF_TYPE == type);

                    if (OCI_STATUS)
                    {
                        typinf->type = OCI_TIF_TYPE;

                        if (OCI_PTYPE_LIST == desc_type)
                        {
                            OCI_EXEC(OCIParamGet((dvoid *)param_root, OCI_DTYPE_PARAM, con->err, (void**)&param_type, (ub4)0))
                        }
                        else
                        {
                            param_type = param_root;
                        }

                        /* get the object type descriptor */

                        OCI_GET_ATTRIB(OCI_DTYPE_PARAM, OCI_ATTR_REF_TDO, param_type, &ref, NULL)

                        OCI_EXEC(OCITypeByRef(typinf->con->env, con->err, ref, OCI_DURATION_SESSION, OCI_TYPEGET_ALL, &typinf->tdo))

                        /* check if it's system predefined type if order to avoid the next call
                        that is not allowed on system types */

                        OCI_GET_ATTRIB(OCI_DTYPE_PARAM, OCI_ATTR_IS_PREDEFINED_TYPE, param_type, &pdt, NULL)

                        if (!pdt)
                        {
                            OCI_GET_ATTRIB(OCI_DTYPE_PARAM, OCI_ATTR_TYPECODE, param_type, &typinf->typecode, NULL)
                        }

                        switch (typinf->typecode)
                        {
                            case  SQLT_NTY:
                        #if OCI_VERSION_COMPILE >= OCI_12_1
                            case  SQLT_REC:
                        #endif
                            {
                                param_list = param_type;
                                ptype      = OCI_DESC_TYPE;
                                attr_type  = OCI_ATTR_LIST_TYPE_ATTRS;
                                num_type   = OCI_ATTR_NUM_TYPE_ATTRS;
                                break;
                            }
                            case SQLT_NCO:
                            {
                                typinf->nb_cols = 1;

                                ptype      = OCI_DESC_COLLECTION;
                                param_cols = param_type;

                                OCI_GET_ATTRIB(OCI_DTYPE_PARAM, OCI_ATTR_COLLECTION_TYPECODE, param_type, &typinf->colcode, NULL)
                                break;
                            }  
                            default:
                            {
                                OCI_STATUS = FALSE;
                                OCI_ExceptionDatatypeNotSupported(con, NULL, typinf->typecode);
                                break;
                            }
                        }
                    }
                    
                    break;
                }
                case OCI_PTYPE_TABLE:
                case OCI_PTYPE_VIEW:
            #if OCI_VERSION_COMPILE >= OCI_10_1
                case OCI_PTYPE_TABLE_ALIAS:
            #endif
                {
                    OCI_STATUS = (((OCI_TIF_TABLE == type) && (OCI_PTYPE_VIEW != desc_type)) ||
                                   ((OCI_TIF_VIEW  == type) && (OCI_PTYPE_VIEW == desc_type)));
 
                    if (OCI_STATUS)
                    {
                        typinf->type = (OCI_PTYPE_VIEW == desc_type ? OCI_TIF_VIEW : OCI_TIF_TABLE);
                        attr_type    = OCI_ATTR_LIST_COLUMNS;
                        num_type     = OCI_ATTR_NUM_COLS;
                        ptype        = OCI_DESC_TABLE; 
                        param_list   = param_root;
                    }
                    
                    break;
                }
                case OCI_PTYPE_SYN:
                {
                    otext *syn_schema_name   = NULL;
                    otext *syn_object_name   = NULL;
                    otext *syn_link_name     = NULL;

                    otext syn_fullname[(OCI_SIZE_OBJ_NAME * 3) + 3] = OTEXT("");

                    /* get link schema, object and database link names */

                    OCI_STATUS = OCI_STATUS && OCI_GetStringAttribute(con, param_root, OCI_DTYPE_PARAM,
                                                                          OCI_ATTR_SCHEMA_NAME, 
                                                                          &syn_schema_name);
                    
                    OCI_STATUS = OCI_STATUS && OCI_GetStringAttribute(con, param_root, OCI_DTYPE_PARAM,
                                                                          OCI_ATTR_NAME, 
                                                                          &syn_object_name);
 
                    OCI_STATUS = OCI_STATUS && OCI_GetStringAttribute(con, param_root, OCI_DTYPE_PARAM,
                                                                         OCI_ATTR_LINK, &syn_link_name);

                    /* compute link full name */

                    OCI_StringGetFullTypeName(syn_schema_name, NULL, syn_object_name, syn_link_name, syn_fullname, (sizeof(syn_fullname) / sizeof(otext)) - 1);

                    /* retrieve the type info of the real object */

                    syn_typinf = OCI_TypeInfoGet (con, syn_fullname, type);
                         
                    /* free temporaRy strings */

                    OCI_MemFree (syn_link_name);
                    OCI_MemFree (syn_object_name);
                    OCI_MemFree (syn_schema_name);
                    
                    /* do we have a valid object ? */

                    OCI_STATUS = (NULL != syn_typinf);

                    break;
                }
            }

            /*  did we handle a supported object other than a synonym */

            if (OCI_STATUS && (OCI_UNKNOWN != ptype))
            {
                /* retrieve the columns parameter if not already retrieved */
                if (param_list)
                {
                    OCI_GET_ATTRIB(OCI_DTYPE_PARAM, attr_type, param_list, &param_cols, NULL)
                    OCI_GET_ATTRIB(OCI_DTYPE_PARAM, num_type, param_list, &typinf->nb_cols, NULL)
                }

                /* allocates memory for cached offsets */

                if (typinf->nb_cols > 0)
                {
                    typinf->offsets = (int *) OCI_MemAlloc(OCI_IPC_ARRAY,  sizeof(*typinf->offsets),
                                                           (size_t) typinf->nb_cols, FALSE);

                    OCI_STATUS = (NULL != typinf->offsets);

                    if (OCI_STATUS)
                    {
                        memset(typinf->offsets, -1, sizeof(*typinf->offsets) * typinf->nb_cols);
                    }
                }

                /* allocates memory for children */

                if (typinf->nb_cols > 0)
                {
                    typinf->cols = (OCI_Column *) OCI_MemAlloc(OCI_IPC_COLUMN,  sizeof(*typinf->cols), 
                                                               (size_t) typinf->nb_cols, TRUE);

                    /* describe children */

                    if (typinf->cols)
                    {
                        for (i = 0; i < typinf->nb_cols; i++)
                        {
                            OCI_STATUS = OCI_STATUS && OCI_ColumnDescribe(&typinf->cols[i], con,
                                                                            NULL, param_cols, i + 1, ptype);

                            OCI_STATUS = OCI_STATUS && OCI_ColumnMap(&typinf->cols[i], NULL);

                            if (!OCI_STATUS)
                            {
                                break;
                            }
                        }
                    }
                    else
                    {
                        OCI_STATUS = FALSE;
                    }
                }
            }
        }
    }

    /* free describe handle */

    if (dschp)
    {
        OCI_HandleFree(dschp, OCI_HTYPE_DESCRIBE);
    }

    /* increment type info reference counter on success */

    if (typinf && OCI_STATUS)
    {
        typinf->refcount++;

        /* type checking sanity checks */

        if ((type != OCI_UNKNOWN) && ((syn_typinf && syn_typinf->type != type) || (!syn_typinf && typinf->type != type)))
        {
            OCI_ExceptionTypeInfoWrongType(con, name);

            OCI_STATUS = FALSE;
        }
    }
        
    /* handle errors */

    if (!OCI_STATUS || syn_typinf)
    {
        OCI_TypeInfoFree(typinf);
        typinf = NULL;
    }


    if (OCI_STATUS)
    {
        OCI_RETVAL = syn_typinf ? syn_typinf : typinf;
    }

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_TypeInfoFree
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_TypeInfoFree
(
    OCI_TypeInfo *typinf
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_TYPE_INFO, typinf)
    OCI_CALL_CONTEXT_SET(typinf->con, NULL, typinf->con->err)

    typinf->refcount--;

    if (typinf->refcount == 0)
    {
        OCI_ListRemove(typinf->con->tinfs, typinf);

        OCI_TypeInfoClose(typinf);

        OCI_FREE(typinf)
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_TypeInfoGetType
 * --------------------------------------------------------------------------------------------- */

unsigned int OCI_API OCI_TypeInfoGetType
(
    OCI_TypeInfo *typinf
)
{
    OCI_GET_PROP(unsigned int, OCI_UNKNOWN, OCI_IPC_TYPE_INFO, typinf, type, typinf->con, NULL, typinf->con->err)
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_TypeInfoGetConnection
 * --------------------------------------------------------------------------------------------- */

OCI_Connection * OCI_API OCI_TypeInfoGetConnection
(
    OCI_TypeInfo *typinf
)
{
    OCI_GET_PROP(OCI_Connection*, NULL, OCI_IPC_TYPE_INFO, typinf, con, typinf->con, NULL, typinf->con->err)
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_TypeInfoGetColumnCount
 * --------------------------------------------------------------------------------------------- */

unsigned int OCI_API OCI_TypeInfoGetColumnCount
(
    OCI_TypeInfo *typinf
)
{
    OCI_GET_PROP(unsigned int, 0, OCI_IPC_TYPE_INFO, typinf, nb_cols, typinf->con, NULL, typinf->con->err)
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_TypeInfoGetColumn
 * --------------------------------------------------------------------------------------------- */

OCI_Column * OCI_API OCI_TypeInfoGetColumn
(
    OCI_TypeInfo *typinf,
    unsigned int  index
)
{
    OCI_CALL_ENTER(OCI_Column *, NULL)
    OCI_CALL_CHECK_PTR(OCI_IPC_TYPE_INFO, typinf)
    OCI_CALL_CHECK_BOUND(typinf->con, index, 1,  typinf->nb_cols)
    OCI_CALL_CONTEXT_SET(typinf->con, NULL, typinf->con->err)

    OCI_RETVAL = &(typinf->cols[index - 1]);

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_TypeInfoGetName
 * --------------------------------------------------------------------------------------------- */

const otext * OCI_API OCI_TypeInfoGetName
(
    OCI_TypeInfo *typinf
)
{
    OCI_GET_PROP(const otext*, NULL, OCI_IPC_TYPE_INFO, typinf, name, typinf->con, NULL, typinf->con->err)
}
