//
// fs_entity.c
//

#include "fs_entity.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>

#include "util_fns.h"

//

const char*
__fs_entity_kind_to_token(
  fs_entity_print_format  format,
  fs_entity_kind          kind
)
{
  static const char*  ascii_kind_str[] = { "D", "F", "#" };
  static const char*  utf8_kind_str[] = { "📁", "📄", "#" };
  
  if ( (format & fs_entity_print_format_ascii) == fs_entity_print_format_ascii ) return ascii_kind_str[kind];
  return utf8_kind_str[kind];
}

//

const char*
__fs_entity_kind_to_string(
  fs_entity_kind          kind
)
{
  static const char*  kind_str[] = { "directory", "file", "unknown" };
  
  return kind_str[kind];
}

//

const char*
__fs_entity_state_to_token(
  fs_entity_print_format  format,
  fs_entity_state         state
)
{
  static const char*    ascii_state_str[] = { "U", "u", "o", "I", "d", "D", "R", "x", "X", "#" };
  static const char*    utf8_state_str[] = { "↑", "⇡", "…", "ℹ", "⇣", "↓", "⇟", "✕", "✖︎", "#" };
  
  if ( (format & fs_entity_print_format_ascii) == fs_entity_print_format_ascii ) return ascii_state_str[state];
  return utf8_state_str[state];
}

//

fs_entity*
__fs_entity_create(
  fs_entity_kind  kind,
  const char      *path
)
{
  fs_entity       *new_entity = NULL;
  
  if ( path && *path && (kind >= fs_entity_kind_directory) && (kind < fs_entity_kind_max) ) {
    size_t        path_len = strlen(path) + 1;
    
    new_entity = malloc(sizeof(fs_entity) + path_len);
    if ( new_entity ) {
      http_ops_method								i_m;
      
      new_entity->kind              = kind;
      new_entity->generation        = 0;
      new_entity->size              = 0;
      new_entity->state             = fs_entity_state_upload;
      new_entity->disabled_states   = 0;
      new_entity->sibling           = NULL;
      new_entity->child             = NULL;
      
      for ( i_m = http_ops_method_get; i_m < http_ops_method_max; i_m++ )
      	new_entity->http_stats[i_m] = http_stats_create();
      
      new_entity->path = ((void*)new_entity) + sizeof(fs_entity);
      strncpy((char*)new_entity->path, path, path_len);
      
      char       *last_slash = strrchr(new_entity->path, '/');
      
      new_entity->name = last_slash ? (last_slash + 1) : new_entity->path;
    }
  }
  return new_entity;
}

//

fs_entity*
__fs_entity_create_with_scanner(
  FTS           *scanner,
  unsigned int  *count
)
{
  fs_entity   *root_entity = NULL;
  FTSENT      *entity_info = fts_read(scanner);
  
  if ( entity_info ) {
    bool      keep_going = true;
    
    do {
      fs_entity   *this_entity = NULL;
      
      switch (entity_info->fts_info) {
      
        case FTS_D:
          this_entity = __fs_entity_create(fs_entity_kind_directory, entity_info->fts_path);
          if ( this_entity ) {
            *count = *count + 1;
            this_entity->size = entity_info->fts_statp->st_size;
            this_entity->child = __fs_entity_create_with_scanner(scanner, count);
          }
          break;
        
        case FTS_DP:
          keep_going = false;
          break;
        
        case FTS_DC:
          fprintf(stderr, "WARNING:  directory cycle would result for %s\n", entity_info->fts_path);
          break;
          
        case FTS_F:
          if ( (entity_info->fts_name[0] != '.') || (strcmp(entity_info->fts_name, ".htaccess") == 0) ) {
            this_entity = __fs_entity_create(fs_entity_kind_file, entity_info->fts_path);
            if ( this_entity ) {
              *count = *count + 1;
              this_entity->size = entity_info->fts_statp->st_size;
            }
          }
          break;
      
      }
      if ( this_entity ) {
        this_entity->sibling = root_entity;
        root_entity = this_entity;
      }
    } while ( keep_going && (entity_info = fts_read(scanner)) );
  }
  return root_entity;
}

//

fs_entity_list*
fs_entity_list_create_with_path(
  const char    *path
)
{
  fs_entity     *root_entity = NULL;
  unsigned int  count = 0;
  struct stat   finfo;
  const char    *base_path = NULL;
  
  if ( stat(path, &finfo) == 0 ) {
    if ( S_ISDIR(finfo.st_mode) ) {
      char          *resolved_path = realpath(path, NULL);
      
      if ( resolved_path ) {
        //
        // Setup for a filesystem traversal:
        //
        const char  *paths[2] = { resolved_path, NULL };
        FTS         *scanner = fts_open(
                                    (char * const *)paths,
                                    FTS_LOGICAL,
                                    NULL
                                  );
        if ( scanner ) {
          root_entity = __fs_entity_create_with_scanner(scanner, &count);
          fts_close(scanner);
        }
        base_path = resolved_path;
      }
    } else if ( S_ISREG(finfo.st_mode) || S_ISLNK(finfo.st_mode) ) {
      root_entity = __fs_entity_create(fs_entity_kind_file, path);
      if ( root_entity ) {
        char        *last_slash;
        
        count++;
        root_entity->size = finfo.st_size;
        
        // The base path should be the parent directory of this item:
        last_slash = strrchr(path, '/');
        if ( last_slash ) {
          char      *sub_path = malloc(last_slash - path);
          if ( sub_path ) {
            memcpy(sub_path, path, last_slash - path);
            sub_path[last_slash - path] = '\0';
            
            base_path = (const char*)sub_path;
          }
        } else {
          base_path = strdup("");
        }
      }
    }
  }
  
  if ( root_entity ) {
    fs_entity_list  *the_list = malloc(sizeof(fs_entity_list));
    
    if ( the_list ) {
      the_list->count             = count;
      the_list->generation        = 0;
      the_list->disabled_states   = (1 << fs_entity_state_download_range);
      the_list->root_entity       = root_entity;
      the_list->base_path         = base_path;
    }
    return the_list;
  }
  return NULL;
}

//

void
__fs_entity_destroy(
  fs_entity     *root_entity
)
{
  if ( root_entity ) {
  	http_ops_method		i_m;
    fs_entity   			*e = root_entity->sibling;
    
    if ( root_entity->child ) __fs_entity_destroy(root_entity->child);
    
    for ( i_m = http_ops_method_get; i_m < http_ops_method_max; i_m++ )
    	http_stats_destroy(root_entity->http_stats[i_m]);
    	
    free(root_entity);
    
    if ( e ) __fs_entity_destroy(e);
  }
}

//

void
fs_entity_list_destroy(
  fs_entity_list    *the_list
)
{
  if ( the_list->base_path ) free((void*)the_list->base_path);
  if ( the_list->root_entity ) __fs_entity_destroy(the_list->root_entity);
  free(the_list);
}

//

unsigned int
__fs_entity_generation_average(
  fs_entity             *root_entity,
  double                mean,
  unsigned long long    *n
)
{
  while ( root_entity ) {
    mean += (root_entity->generation - mean) / *n;
    *n = *n + 1;
    
    if ( root_entity->child ) mean = __fs_entity_generation_average(root_entity->child, mean, n);
    root_entity = root_entity->sibling;
  }
  return mean;
}

//

unsigned int
fs_entity_generation_average(
  fs_entity   *root_entity
)
{
  unsigned long long    n = 1;
  double                avg = __fs_entity_generation_average(root_entity, 0.0, &n);
  
  return (unsigned int)ceil(avg);
}

//

void
fs_entity_fprint(
  FILE                    *fptr,
  fs_entity_print_format  format,
  fs_entity               *entity
)
{
  if ( (format & fs_entity_print_format_kind) == fs_entity_print_format_kind ) {
    fprintf(fptr, "%s ", __fs_entity_kind_to_token(format, entity->kind));
  }
  
  if ( (format & fs_entity_print_format_generation) == fs_entity_print_format_generation ) {
    if ( (format & fs_entity_print_format_state) == fs_entity_print_format_state ) {
      fprintf(fptr, "[%04d %s] ", entity->generation, __fs_entity_state_to_token(format, entity->state));
    } else {
      fprintf(fptr, "[%04d] ", entity->generation);
    }
  } else if ( (format & fs_entity_print_format_state) == fs_entity_print_format_state ) {
      fprintf(fptr, "[%s] ", __fs_entity_state_to_token(format, entity->state));
  }
  
  if ( (format & fs_entity_print_format_size) == fs_entity_print_format_size ) {
    fprintf(fptr, "%-8llu ", (unsigned long long)entity->size);
  }
  
  if ( (format & fs_entity_print_format_name) == fs_entity_print_format_name ) {
    if ( (format & fs_entity_print_format_path) == fs_entity_print_format_path ) {
      fprintf(fptr, "%s (%s)\n", entity->name, entity->path);
    } else {
      fprintf(fptr, "%s\n", entity->name);
    }
  } else if ( (format & fs_entity_print_format_path) == fs_entity_print_format_path ) {
    fprintf(fptr, "%s\n", entity->path);
  }
}

//

void
fs_entity_print(
  fs_entity_print_format  format,
  fs_entity               *entity
)
{
  return fs_entity_fprint(stdout, format, entity);
}

//

bool
fs_entity_get_state_is_enabled(
  fs_entity         *entity,
  fs_entity_state   state
)
{
  if ( state >= fs_entity_state_upload && state < fs_entity_state_max ) {
    return ((1 << state) & entity->disabled_states) ? false : true;
  }
  return false;
}

//

void
fs_entity_set_state_is_enabled(
  fs_entity         *entity,
  fs_entity_state   state,
  bool              is_enabled
)
{
  if ( state >= fs_entity_state_upload && state < fs_entity_state_max ) {
    if ( is_enabled ) {
      entity->disabled_states &= ~(1 << state);
    } else {
      entity->disabled_states |= (1 << state);
    }
  }
}

//

void
__fs_entity_fprint(
  FILE                    *fptr,
  fs_entity_print_format  format,
  fs_entity               *entity,
  int                     indent
)
{
  fs_entity     *e = entity;
  const char    *indent_str = ( (format & fs_entity_print_format_ascii) == fs_entity_print_format_ascii ) ? "|-" : "├ ";
  
  while ( e ) {
    int         i;
    char        kind;
    
    i = indent;
    while ( i-- ) fprintf(fptr, "%s", indent_str);
    
    fs_entity_fprint(fptr, format, e);
    
    if ( e->kind == fs_entity_kind_directory ) __fs_entity_fprint(fptr, format, e->child, indent + 1);
    e = e->sibling;
  }
}

//

void
fs_entity_list_fprint(
  FILE                    *fptr,
  fs_entity_print_format  format,
  fs_entity_list          *the_list
)
{
  fprintf(fptr, "%s base path '%s'\n", ( (format & fs_entity_print_format_ascii) == fs_entity_print_format_ascii ) ? " _" : "┌ ", the_list->base_path);
  __fs_entity_fprint(fptr, format, the_list->root_entity, 0);
  if ( (format & fs_entity_print_format_summary) == fs_entity_print_format_summary ) {
    const char    *indent_str = ( (format & fs_entity_print_format_ascii) == fs_entity_print_format_ascii ) ? "|_" : "└ ";
    
    fprintf(fptr, "%s%u node%s, generation %u\n", indent_str, the_list->count, (the_list->count == 1) ? "" : "s", the_list->generation);
  }
}

//

void
fs_entity_list_print(
  fs_entity_print_format  format,
  fs_entity_list          *the_list
)
{
  fs_entity_list_fprint(stdout, format, the_list);
}

//

void
fs_entity_list_advance_entity_state(
  fs_entity_list      *the_list,
  fs_entity           *root_entity
)
{
  fs_entity_state     start_state = root_entity->state;
  
try_again:

  switch ( root_entity->kind ) {
  
    case fs_entity_kind_directory:
      switch ( root_entity->state ) {
          
        case fs_entity_state_upload:
          root_entity->state = fs_entity_state_upload_sub;
          break;
      
        case fs_entity_state_upload_sub:
          root_entity->state = fs_entity_state_options;
          break;
        
        case fs_entity_state_options:
          root_entity->state = fs_entity_state_getinfo;
          break;
          
        case fs_entity_state_getinfo:
          root_entity->state = fs_entity_state_download_sub;
          break;
        
        case fs_entity_state_download_sub:
          root_entity->state = fs_entity_state_download;
          break;
        
        case fs_entity_state_download:
          root_entity->state = fs_entity_state_delete_sub;
          break;
        
        case fs_entity_state_delete_sub:
          root_entity->state = fs_entity_state_delete;
          break;
        
        case fs_entity_state_delete:
          root_entity->generation++;
          root_entity->state = fs_entity_state_upload;
          break;
      
        case fs_entity_state_download_range:
        case fs_entity_state_max:
          break;
      
      }
      break;
    
    case fs_entity_kind_file:
      switch ( root_entity->state ) {
      
        case fs_entity_state_upload:
          root_entity->state = fs_entity_state_options;
          break;
          
        case fs_entity_state_options:
          root_entity->state = fs_entity_state_getinfo;
          break;
          
        case fs_entity_state_getinfo:
          root_entity->state = fs_entity_state_download;
          break;
        
        case fs_entity_state_download:
          root_entity->state = fs_entity_state_download_range;
          break;
        
        case fs_entity_state_download_range:
          root_entity->state = fs_entity_state_delete;
          break;
        
        case fs_entity_state_delete:
          root_entity->generation++;
          root_entity->state = fs_entity_state_upload;
          break;
        
        
        case fs_entity_state_upload_sub:
        case fs_entity_state_download_sub:
        case fs_entity_state_delete_sub:
        case fs_entity_state_max:
          break;
      
      }
      break;
    
    case fs_entity_kind_max:
      break;
  
  }
  //
  // Skip disabled states...
  //
  if ( (((1 << root_entity->state) & (root_entity->disabled_states)) != 0) || (((1 << root_entity->state) & (the_list->disabled_states)) != 0) ) {
    //
    // ...but only so long as we haven't looped back around to the state
    // when this function was entered!
    //
    if ( root_entity->state != start_state ) goto try_again;
    fprintf(stderr, "ERROR:  illegal state disablement configuration -- loop detected\n");
    exit(EINVAL);
  }
}

//

fs_entity*
__fs_entity_next_node(
  fs_entity_list  *the_list,
  fs_entity       *root_entity,
  unsigned int    generation
)
{
  fs_entity     *e;
  fs_entity     *node = NULL;
  unsigned int  n = 0;
  unsigned int  min_gen = generation;
  unsigned int  iteration = 0;
  
  e = root_entity;
  while ( e ) {
    n++;
    if ( e->generation < min_gen ) min_gen = e->generation;
    e = e->sibling;
  }
  
  // Everything in this row is current generation.
  if ( min_gen == generation ) return NULL;
  
  e = root_entity;
  while ( e ) {
    if ( e->generation < generation ) {
      switch ( e->kind ) {
      
        case fs_entity_kind_directory: {
          //
          // For directories in a *_sub state, first try for a child element:
          //
          switch ( e->state ) {
          
            case fs_entity_state_upload_sub:
            case fs_entity_state_download_sub:
            case fs_entity_state_delete_sub: {
              node = __fs_entity_next_node(the_list, e->child, generation);
              //
              // If nothing was returned, then the child chain has completed and
              // this node can step forward and be returned:
              //
              if ( ! node ) {
                fs_entity_list_advance_entity_state(the_list, e);
                // If the state advanced into the fs_entity_state_download_sub state 
                // (from fs_entity_state_upload_sub) and no children were waiting to
                // change state then it's implied that theres nothing to download
                // anyway, so advance again: 
                if ( e->state == fs_entity_state_download_sub ) fs_entity_list_advance_entity_state(the_list, e);
                node = e;
              }
              return node;
            }
            
            default:
              return e;
          
          }
          break;
        }
        
        case fs_entity_kind_file:
          return e;
      
        case fs_entity_kind_max:
          break;
      }
    }
    e = e->sibling;
    
    if ( ! e ) {
      iteration++;
      e = root_entity;
    }
  }
  
  // Should never get here:
  return NULL;
}

//

fs_entity*
fs_entity_list_next_node(
  fs_entity_list  *the_list,
  unsigned int    max_generation
)
{
  if ( the_list->generation < max_generation ) {
    //
    // Calculate the average hit count:
    //
    double        avg = fs_entity_generation_average(the_list->root_entity);
    
    // If the average is > the generation, increase the generation:
    if ( avg >= (double)(1 + the_list->generation) ) {
      the_list->generation++;
    }
    if ( the_list->generation < max_generation ) return __fs_entity_next_node(the_list, the_list->root_entity, 1 + the_list->generation);
  }
  return NULL;
}

//

fs_entity*
__fs_entity_random_node(
  fs_entity_list  *the_list,
  fs_entity       *root_entity,
  unsigned int    generation
)
{
  fs_entity     *e;
  fs_entity     *node = NULL;
  unsigned int  n = 0;
  unsigned int  min_gen = generation;
  unsigned int  iteration = 0;
  
  e = root_entity;
  while ( e ) {
    n++;
    if ( e->generation < min_gen ) min_gen = e->generation;
    e = e->sibling;
  }
  
  // Everything in this row is current generation.
  if ( min_gen == generation ) return NULL;
  
  e = root_entity;
  while ( e ) {
    if ( e->generation < generation ) {
      //
      // This node is eligible, should we use it?
      //
      if ( (iteration == 20) || (random_long_int() % n == 0) ) {
        switch ( e->kind ) {
        
          case fs_entity_kind_directory: {
            //
            // For directories in a *_sub state, first try for a child element:
            //
            switch ( e->state ) {
            
              case fs_entity_state_upload_sub:
              case fs_entity_state_download_sub:
              case fs_entity_state_delete_sub: {
                node = __fs_entity_random_node(the_list, e->child, generation);
                //
                // If nothing was returned, then the child chain has completed and
                // this node can step forward and be returned:
                //
                if ( ! node ) {
                  fs_entity_list_advance_entity_state(the_list, e);
                  // If the state advanced into the fs_entity_state_download_sub state 
                  // (from fs_entity_state_upload_sub) and no children were waiting to
                  // change state then it's implied that theres nothing to download
                  // anyway, so advance again: 
                  if ( e->state == fs_entity_state_download_sub ) fs_entity_list_advance_entity_state(the_list, e);
                  node = e;
                }
                return node;
              }
              
              default:
                return e;
            
            }
            break;
          }
          
          case fs_entity_kind_file:
            return e;
        
          case fs_entity_kind_max:
            break;
        }
      }
    }
    e = e->sibling;
    
    if ( ! e ) {
      iteration++;
      e = root_entity;
    }
  }
  
  // Should never get here:
  return NULL;
}

//

fs_entity*
fs_entity_list_random_node(
  fs_entity_list  *the_list,
  unsigned int    max_generation
)
{
  if ( the_list->generation < max_generation ) {
    //
    // Calculate the average hit count:
    //
    double        avg = fs_entity_generation_average(the_list->root_entity);
    
    // If the average is > the generation, increase the generation:
    if ( avg >= (double)(1 + the_list->generation) ) {
      the_list->generation++;
    }
    if ( the_list->generation < max_generation ) return __fs_entity_random_node(the_list, the_list->root_entity, 1 + the_list->generation);
  }
  return NULL;
}

//

bool
fs_entity_list_get_state_is_enabled(
  fs_entity_list      *the_list,
  fs_entity_state     state
)
{
  if ( state >= fs_entity_state_upload && state < fs_entity_state_max ) {
    return ((1 << state) & the_list->disabled_states) ? false : true;
  }
  return false;
}

//

void
fs_entity_list_set_state_is_enabled(
  fs_entity_list      *the_list,
  fs_entity_state     state,
  bool                is_enabled
)
{
  if ( state >= fs_entity_state_upload && state < fs_entity_state_max ) {
    if ( is_enabled ) {
      the_list->disabled_states &= ~(1 << state);
    } else {
      the_list->disabled_states |= (1 << state);
    }
  }
}

//

const char*
fs_entity_list_url_for_entity(
  fs_entity_list    *the_list,
  const char        *base_url,
  fs_entity         *the_entity
)
{
  char              *url;
  const char        *path = the_entity->path + strlen(the_list->base_path);
  bool              path_has_leading_slash = (*path == '/') ? true : false;
  bool              url_has_trailing_slash = (base_url[strlen(base_url) - 1] == '/') ? true : false;
  const char        *trailing = (the_entity->kind == fs_entity_kind_directory) ? "/" : NULL;
  
  if ( path_has_leading_slash ) {
    if ( url_has_trailing_slash ) {
      url = strmcat(base_url, path + 1, trailing, NULL);
    } else {
      url = strmcat(base_url, path, trailing, NULL);
    }
  }
  else if ( url_has_trailing_slash ) {
    url = strmcat(base_url, path, trailing, NULL);
  }
  else {
    if ( strlen(path) > 0 ) {
      url = strmcat(base_url, "/", path, trailing, NULL);
    } else {
      url = strmcat(base_url, trailing, NULL);
    }
  }
  return (const char*)url;
}

//

void
fs_entity_list_stats_print(
  http_stats_format 			format,
  http_stats_print_flags	flags, 
  fs_entity_list          *the_list
)
{
  fs_entity_list_stats_fprint(stdout, format, flags, the_list);
}

//

void
__fs_entity_stats_fprint(
  FILE                    *fptr,
  http_stats_format 			format,
  http_stats_print_flags	flags, 
  fs_entity               *entity
)
{
	switch ( format ) {
	
		case http_stats_format_table: {
			while ( entity ) {
				http_ops_method				i_m;
		
				if ( (format & fs_entity_print_format_kind) == fs_entity_print_format_kind ) {
					fprintf(fptr, "%s ", __fs_entity_kind_to_token(fs_entity_print_format_ascii, entity->kind));
				}
		
				if ( (format & fs_entity_print_format_name) == fs_entity_print_format_name ) {
					if ( (format & fs_entity_print_format_path) == fs_entity_print_format_path ) {
						fprintf(fptr, "%s (%s)\n", entity->name, entity->path);
					} else {
						fprintf(fptr, "%s\n", entity->name);
					}
				} else if ( (format & fs_entity_print_format_path) == fs_entity_print_format_path ) {
					fprintf(fptr, "%s\n", entity->path);
				}
				for ( i_m = http_ops_method_get; i_m < http_ops_method_max; i_m++ ) {
					if ( ! http_stats_is_empty(entity->http_stats[i_m]) ) {
						fprintf(fptr, "[%s]\n", http_ops_method_get_string(i_m));
						http_stats_fprint(fptr, format, flags, entity->http_stats[i_m]);
						fprintf(fptr, "\n");
					}
				}
		
				// Handle all children:
				if ( (entity->kind == fs_entity_kind_directory) && entity->child ) __fs_entity_stats_fprint(fptr, format, flags, entity->child);
	
				// Next sibling:
				entity = entity->sibling;
			}
			break;
		}
				
		case http_stats_format_csv:
		case http_stats_format_tsv: {
			char						delim;
		
			switch ( format ) {
				case http_stats_format_csv: delim = ','; break;
				case http_stats_format_tsv: delim = '\t'; break;
				
				case http_stats_format_table:
				case http_stats_format_max:
					break;
			}
			while ( entity ) {
				http_ops_method				i_m;
				
				for ( i_m = http_ops_method_get; i_m < http_ops_method_max; i_m++ ) {
					fprintf(fptr,
							"\"%s\"%c\"%s\"%c\"%s\"%c",
							__fs_entity_kind_to_string(entity->kind), delim,
							entity->path, delim,
							http_ops_method_get_string(i_m), delim
						);
					http_stats_fprint(fptr, format, (flags | http_stats_print_flags_no_header) & ~http_stats_print_flags_header_only, entity->http_stats[i_m]);
				}
		
				// Handle all children:
				if ( (entity->kind == fs_entity_kind_directory) && entity->child ) __fs_entity_stats_fprint(fptr, format, (flags | http_stats_print_flags_no_header) & ~http_stats_print_flags_header_only, entity->child);
	
				// Next sibling:
				entity = entity->sibling;
			}
			break;
		}
		
		case http_stats_format_max:
			break;
		
	}
}

void
fs_entity_list_stats_fprint(
  FILE                    *fptr,
  http_stats_format 			format,
  http_stats_print_flags	flags, 
  fs_entity_list          *the_list
)
{
	if ( (flags & http_stats_print_flags_no_header) != http_stats_print_flags_no_header ) {
		switch ( format ) {
			case http_stats_format_table:
				if ( (flags & http_stats_print_flags_header_only) == http_stats_print_flags_header_only ) return;
				break;
				
			case http_stats_format_csv:
			case http_stats_format_tsv: {
				char						delim;
			
				switch ( format ) {
					case http_stats_format_csv: delim = ','; break;
					case http_stats_format_tsv: delim = '\t'; break;
					
					case http_stats_format_table:
					case http_stats_format_max:
					  break;
				}
				fprintf(fptr, "\"kind\"%1$c\"path\"%1$c\"method\"%1$c", delim);
				http_stats_fprint(fptr, format, http_stats_print_flags_header_only, NULL);
				if ( (flags & http_stats_print_flags_header_only) == http_stats_print_flags_header_only ) return;
				break;
			}
			
			case http_stats_format_max:
				break;
		}
	}
  __fs_entity_stats_fprint(fptr, format, flags, the_list->root_entity);
}
