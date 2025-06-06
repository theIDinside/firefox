{%- import "macros.py" as py %}

{%- if ci.has_callback_definitions() %}
{%- include "CallbackInterfaceRuntime.py" %}
{%- endif %}

{%- for type_ in ci.iter_local_types() %}
{%- let type_name = type_|type_name %}
{%- let ffi_converter_name = type_|ffi_converter_name %}
{%- let canonical_type_name = type_|canonical_name %}

{#
 # Map `Type` instances to an include statement for that type.
 #
 # There is a companion match in `PythonCodeOracle::create_code_type()` which performs a similar function for the
 # Rust code.
 #
 #   - When adding additional types here, make sure to also add a match arm to that function.
 #   - To keep things manageable, let's try to limit ourselves to these 2 mega-matches
 #}
{%- match type_ %}

{%- when Type::Boolean %}
{%- include "BooleanHelper.py" %}

{%- when Type::Int8 %}
{%- include "Int8Helper.py" %}

{%- when Type::Int16 %}
{%- include "Int16Helper.py" %}

{%- when Type::Int32 %}
{%- include "Int32Helper.py" %}

{%- when Type::Int64 %}
{%- include "Int64Helper.py" %}

{%- when Type::UInt8 %}
{%- include "UInt8Helper.py" %}

{%- when Type::UInt16 %}
{%- include "UInt16Helper.py" %}

{%- when Type::UInt32 %}
{%- include "UInt32Helper.py" %}

{%- when Type::UInt64 %}
{%- include "UInt64Helper.py" %}

{%- when Type::Float32 %}
{%- include "Float32Helper.py" %}

{%- when Type::Float64 %}
{%- include "Float64Helper.py" %}

{%- when Type::String %}
{%- include "StringHelper.py" %}

{%- when Type::Bytes %}
{%- include "BytesHelper.py" %}

{%- when Type::Enum { name, module_path } %}
{%- let e = ci.get_enum_definition(name).unwrap() %}
{# For enums, there are either an error *or* an enum, they can't be both. #}
{%- if ci.is_name_used_as_error(name) %}
{%- include "ErrorTemplate.py" %}
{%- else %}
{%- include "EnumTemplate.py" %}
{% endif %}

{%- when Type::Record { name, module_path } %}
{%- include "RecordTemplate.py" %}

{%- when Type::Timestamp %}
{%- include "TimestampHelper.py" %}

{%- when Type::Duration %}
{%- include "DurationHelper.py" %}

{%- when Type::Optional { inner_type } %}
{%- include "OptionalTemplate.py" %}

{%- when Type::Sequence { inner_type } %}
{%- include "SequenceTemplate.py" %}

{%- when Type::Map { key_type, value_type } %}
{%- include "MapTemplate.py" %}

{%- when Type::CallbackInterface { name, module_path } %}
{%- include "CallbackInterfaceTemplate.py" %}

{%- when Type::Custom { name, module_path, builtin } %}
{%- if ci.is_external(type_) %}
{%- include "ExternalTemplate.py" %}
{%- else %}
{%- include "CustomType.py" %}
{%- endif %}

{%- else %}
{%- endmatch %}
{%- endfor %}

# objects.
{%- for type_ in ci.filter_local_types(self.iter_sorted_object_types()) %}
{%- match type_ %}
{%- when Type::Object { name, .. } %}
{%-     let type_name = type_|type_name %}
{%-     let ffi_converter_name = type_|ffi_converter_name %}
{%-     let canonical_type_name = type_|canonical_name %}
{%-     include "ObjectTemplate.py" %}
{%- else %}
{%- endmatch %}
{%- endfor %}

{%- for type_ in ci.iter_external_types() %}
{%- let name = type_.name().unwrap() %}
{%- include "ExternalTemplate.py" %}
{%- endfor %}
{#-
Setup type aliases for our custom types, has complications due to
forward type references, #2067
-#}
{%- for (name, ty) in self.get_custom_type_aliases() %}
{{ name }} = {{ ty|type_name }}
{%- endfor %}
