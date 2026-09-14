"""Regression tests for the project-owned type naming check."""

from pathlib import Path
import sys
import unittest
from unittest.mock import patch

with patch.object(sys, "path", [str(Path(__file__).resolve().parents[1]), *sys.path]):
    import CheckTypeNames as type_names


class FArdaTypeNameTests(unittest.TestCase):
    def names(self, source):
        return [item.name for item in type_names.violations(source)]

    def test_finds_nested_local_alias_enum_and_aligned_types(self):
        source = """struct FArdaOwner {
            struct FState {};
            enum class EMode { Idle };
            using Result = int;
            void Run() { struct alignas(16) FLocal {}; }
        };
        struct FArdaOwner::FInstanceState {};
        typedef void (*Callback)(int);
        union FStorage { int X; };
        """
        self.assertEqual(self.names(source), ["FState", "EMode", "Result", "FLocal",
                                             "FInstanceState", "Callback", "FStorage"])

    def test_ignores_strings_comments_template_parameters_and_sdk_imports(self):
        source = '''// struct FWrong {};
        /* using Wrong = int; */
        const char* Message = R"note(struct FWrong {}; "using Bad = int;")note";
        template <class Value, template <class> class Container>
        struct TArdaCollection { using FArdaValue = Value; };
        using Microsoft::WRL::ComPtr;
        using namespace eastl;
        struct GLFWwindow;
        struct FArdaLocal final {};
        enum class EArdaMode { Idle };
        '''
        self.assertEqual(self.names(source), [])

    def test_external_forward_exception_does_not_hide_owned_definitions(self):
        self.assertEqual(self.names("struct GLFWwindow { int Wrong; };"), ["GLFWwindow"])

    def test_finds_permutation_macro_types(self):
        source = '''ARDA_SHADER_PERMUTATION_BOOL(FUseFeature, "FEATURE");
        ARDA_SHADER_PERMUTATION_INT(FQuality, "QUALITY", 3);
        ARDA_SHADER_PERMUTATION_BOOL(FArdaValidFeature, "VALID");
        '''
        self.assertEqual(self.names(source), ["FUseFeature", "FQuality"])

    def test_finds_anonymous_aggregate_typedef_aliases_without_reporting_fields(self):
        source = '''typedef struct {
            int Member;
            union { int Integer; float Fraction; } Value;
        } Bad, *BadPointer;
        typedef union FArdaStorage { int Member; } Other;
        typedef struct { using Inner = int; } Outer;
        '''
        self.assertEqual(self.names(source), ["Bad", "BadPointer", "Other", "Inner", "Outer"])

    def test_finds_every_typedef_declarator_and_ignores_type_arguments(self):
        source = '''typedef int First, *Second, Array[4];
        typedef eastl::pair<int, float> Pair, *PairPointer;
        typedef void (*Callback)(int, float), (*OtherCallback)(long);
        typedef void Function(int, float), (*FunctionPointer)(int);
        typedef decltype(MakeValue(1, 2)) Value, *ValuePointer;
        typedef void UserPointerFunction(FArdaBuffer *Parameter);
        typedef void SdkPointerFunction(DWORD *Parameter);
        typedef void (CUDAAPI *NativeCallback)(int);
        '''
        self.assertEqual(self.names(source), ["Array", "First", "Second", "Pair", "PairPointer",
                                             "Callback", "OtherCallback", "Function", "FunctionPointer",
                                             "Value", "ValuePointer", "UserPointerFunction",
                                             "SdkPointerFunction", "NativeCallback"])

    def test_handles_attributed_aliases_and_balanced_alignment(self):
        source = '''using Bad [[deprecated("Use FArdaGood")]] = int;
        using FArdaGood [[maybe_unused]] = int;
        struct alignas(alignof(eastl::pair<int, float>)) BadAlignment {};
        struct [[deprecated]] alignas((sizeof(int) + 15) & ~15) OtherAlignment {};
        template<class T> using BadTemplate [[deprecated]] = eastl::vector<T>;
        '''
        self.assertEqual(self.names(source), ["Bad", "BadAlignment", "OtherAlignment", "BadTemplate"])

    def test_checks_macro_generated_types_but_not_formal_parameters(self):
        source = """#define ARDA_MAKE(Name) \\
        struct Name { using FBadAlias = int; };
        ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)
        ARDA_END_SHADER_PARAMETER_STRUCT()
        ARDA_CUDA_PARAMETER_STRUCT(FArdaCudaParameters, FIELDS)
        """
        self.assertEqual(self.names(source), ["FBadAlias", "FParameters"])

    def test_keeps_macro_formals_distinct_from_final_and_incomplete_typedefs(self):
        source = """#define ARDA_DIMENSION(Name) \\
        struct Name final { using BadAlias = int; };
        #define ARDA_BEGIN(Name) \\
        struct Name { typedef FArdaAnchor
        #define ARDA_OTHER(Name) \\
        struct Name { Name() = default; using OtherBadAlias = int; };
        """
        self.assertEqual(self.names(source), ["BadAlias", "OtherBadAlias"])

    def test_checks_inactive_preprocessor_branches_and_preserves_lines(self):
        items = type_names.violations("#if 0\nstruct FWrong {};\n#endif\nusing Wrong = int;\n")
        self.assertEqual([(item.name, item.line) for item in items], [("FWrong", 2), ("Wrong", 4)])


if __name__ == "__main__":
    unittest.main()
