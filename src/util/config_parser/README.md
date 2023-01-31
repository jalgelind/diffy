Config parser:
--------------

The libraries I found do not handle comments when serializing. I kinda want that.

Those libraries did not properly retain insert-order on tables, so values in the
configuration file would be automatically sorted when re-serializing due to key
iteration order.

Writing this was fun, but it should probably be replaced with json or something.

Known bugs/limitations:
-----------------------

* Some comments are lost when deserializing a file and serializing it again.

When we deserialize into a value tree, we try to attach comments to the
logically closest context. Some comments are not preserved, as we don't
have anywhere to put them (trailing comments, comment to the right of the defined last
value).


Example code and output
-----------------------

    std::string cfg_tmp = R"foo(
    # 1 (above section)
    [section] # 2 (next to section)
    # 3 (above key, below parent section)
    key = "value" # 4 (next to "value")
    # 5 (above something, after key)
    something = { # 6 (next to something)
            # 7 (below parent something, before apa)
            apa = "bepa" # 8 (next to "bepa")
            # 9 (below apa, last item in table)
    } # 10 (next to something-close-brace)
    # 11 (trailing comment)
    )foo";

    fmt::print("original:\n{}\n", cfg_tmp);

    ParseResult result;
    cfg_parse_dump(cfg_tmp, result);

    Value obj;
    cfg_parse_value_tree(cfg_tmp, result, obj);

    fmt::print("tree:\n");
    fmt::print("{}", cfg_dump_value_object(obj));

    fmt::print("re-serialized:\n{}\n", cfg_serialize(obj));
    fmt::print("TODO: remove section indentation and trailing empty lines\n");

    if (auto tmp = obj.lookup_value_by_path({"section", "something", "apa"}); tmp != std::nullopt) {
            fmt::print("1 Found: {}\n", repr(*tmp));
    }

    if (auto tmp = obj.lookup_value_by_path({"section", "something", "nope"}); tmp != std::nullopt) {
            fmt::print("2 Found: {}\n", repr(*tmp));
    }

    if (auto tmp = obj.lookup_value_by_path("section.something.apa"); tmp != std::nullopt) {
            fmt::print("3 Found: {}\n", repr(*tmp));
            if (tmp->get().is_string()) {
            fmt::print("It's a string: {}\n", tmp->get().as_string());
            }
    }

    if (auto tmp = obj.lookup_value_by_path("section.something.nope"); tmp != std::nullopt) {
            fmt::print("4 Found: {}\n", repr(*tmp));
    }


Outputs

    original:

    # 1 (above section)
    [section] # 2 (next to section)
    # 3 (above key, below parent section)
    key = "value" # 4 (next to "value")
    # 5 (above something, after key)
    something = { # 6 (next to something)
            # 7 (below parent something, before apa)
            apa = "bepa" # 8 (next to "bepa")
            # 9 (below apa, last item in table)
    } # 10 (next to something-close-brace)
    # 11 (trailing comment)
    
    0 | 0 Comment  1 (above section) 
    1 | 0 Key section 
    2 | 0 TableStart from section 
    3 | 1     Comment  2 (next to section) 
    4 | 1     Comment  3 (above key, below parent section) 
    5 | 1     Key key 
    6 | 1     Value value String
    7 | 1     Comment  4 (next to "value") 
    8 | 1     Comment  5 (above something, after key) 
    9 | 1     Key something 
    10 | 1     TableStart  
    11 | 2         Comment  6 (next to something) 
    12 | 2         Comment  7 (below parent something, before apa) 
    13 | 2         Key apa 
    14 | 2         Value bepa String
    15 | 2         Comment  8 (next to "bepa") 
    16 | 2         Comment  9 (below apa, last item in table) 
    17 | 2         Comment  10 (next to something-close-brace) 
    18 | 2         Comment  11 (trailing comment) 
    19 | 1     TableEnd  
    20 | 0 TableEnd  
    tree:
    Comments:
    V:  1 (above section)
    Value:
    Table {
    Key: section
    Comments:
            V:  2 (next to section)
    Value:
            Table {
            Key: key
            Comments:
                    K:  3 (above key, below parent section)
                    V:  4 (next to "value")
            Value:
            'value'

            Key: something
            Comments:
                    K:  5 (above something, after key)
                    V:  6 (next to something)
            Value:
            Table {
                    Key: apa
                    Comments:
                    K:  7 (below parent something, before apa)
                    V:  8 (next to "bepa")
                    Value:
                    'bepa'

            }

            }

    }
    re-serialized:
    # 1 (above section)
    [section] # 2 (next to section)

    # 3 (above key, below parent section)
    key = 'value' # 4 (next to "value")

    # 5 (above something, after key)
    something = { # 6 (next to something)

    # 7 (below parent something, before apa)
    apa = 'bepa' # 8 (next to "bepa")
    }
