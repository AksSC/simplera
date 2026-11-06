# Relational Algebra Grammar

> **Historical starter reference.** This grammar predates the 2026 project extensions and is not a complete specification of the current parser. See the [README](README.md) and [phase reports](docs/Report2.md) for working examples and added commands.

## Grammar

```
Statement -> relation_name <- assignment_statement
           | non_assignment_statement

assignment_statement -> cross_product_statement
                      | distinct_statement
                      | join_statement
                      | projection_statement
                      | selection_statement
                      | sort_statement
                       
non_assignment_statement -> clear_statement 
                           | index_statement
                           | list_statement
                           | load_statement
                           | print_statement
                           | quit_statement
                           | rename_statement
                           | source_statement

cross_product_statement -> CROSS relation_name relation_name

distinct_statement -> DISTINCT relation_name

join_statement -> JOIN relation_name, relation_name ON join_condition [WHERE selection_condition] [PROJECT attribute_list];

join_condition -> relation_name.column_name == relation_name.column_name
                | relation_name.column_name + relation_name.column_name == int_literal
                | relation_name.column_name - relation_name.column_name == int_literal

selection_condition -> relation_name.column_name binop int_literal

attribute_list -> relation_name.column_name
                | attribute_list, relation_name.column_name

projection_statement -> PROJECT projection_list FROM relation_name

projection_list -> projection_list, column_name 
                 | column_name

selection_statement -> SELECT condition FROM relation_name

condition -> column_name binop column_name 
           | column_name binop int_literal

binop -> > | < | == | != | <= | >= | => | =< 

sort_statement -> SORT relation_name BY column_name IN sorting_order

sorting_order -> ASC | DESC

clear_statement -> CLEAR relation_name

index_statement -> INDEX ON column_name FROM relation_name USING indexing_strategy

indexing_strategy -> HASH | BTREE | NOTHING;

list_statement -> LIST TABLES;

load_statement -> LOAD relation_name

print_statement -> PRINT relation_name

quit_statement -> QUIT

rename_statement -> RENAME column_name TO column_name FROM relation_name

source_statement -> SOURCE file_name

```
