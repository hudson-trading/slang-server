import eslint from '@eslint/js'
import tsparser from '@typescript-eslint/parser'
import tseslint from '@typescript-eslint/eslint-plugin'
import unusedImports from 'eslint-plugin-unused-imports'
import globals from 'globals'

export default [
  {
    ignores: ['out/**', 'dist/**', '**/*.d.ts', 'src/test/**'],
  },
  eslint.configs.recommended,
  {
    files: ['src/**/*.ts'],
    languageOptions: {
      parser: tsparser,
      parserOptions: {
        ecmaVersion: 6,
        sourceType: 'module',
        project: './tsconfig.json',
      },
      globals: {
        ...globals.nodeBuiltin,
      },
    },
    plugins: {
      '@typescript-eslint': tseslint,
      'unused-imports': unusedImports,
    },
    rules: {
      '@typescript-eslint/naming-convention': [
        'error',
        {
          selector: 'enumMember',
          format: ['PascalCase'],
        },
      ],
      '@typescript-eslint/semi': 'off',
      '@typescript-eslint/no-unused-vars': [
        'error',
        {
          argsIgnorePattern: '^_',
          varsIgnorePattern: '^_',
        },
      ],
      '@typescript-eslint/explicit-function-return-type': 'off',
      // '@typescript-eslint/no-explicit-any': 'warn',
      // "@typescript-eslint/no-non-null-assertion": "warn",
      // "@typescript-eslint/prefer-optional-chain": "warn",
      // "@typescript-eslint/prefer-nullish-coalescing": "warn",
      '@typescript-eslint/no-floating-promises': 'error',
      '@typescript-eslint/await-thenable': 'error',
      '@typescript-eslint/no-misused-promises': 'error',
      curly: 'warn',
      eqeqeq: 'warn',
      'no-throw-literal': 'warn',
      semi: 'off',
      'no-extra-semi': 'error',
      'no-unused-vars': 'off',
      'no-undef': 'warn',
      'no-constant-condition': 'warn',
      'unused-imports/no-unused-imports': 'error',
    },
  },
]
