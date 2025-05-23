export const description = `
Increment and decrement statement tests.
`;

import { makeTestGroup } from '../../../../common/framework/test_group.js';
import { TypedArrayBufferView } from '../../../../common/util/util.js';
import { AllFeaturesMaxLimitsGPUTest, GPUTest } from '../../../gpu_test.js';
import { kValue } from '../../../util/constants.js';

export const g = makeTestGroup(AllFeaturesMaxLimitsGPUTest);

/**
 * Builds, runs then checks the output of a statement shader test.
 *
 * @param t The test object
 * @param builder The shader builder function that takes a
 * StatementTestBuilder as the single argument, and returns either a WGSL
 * string which is embedded into the WGSL entrypoint function, or a structure
 * with entrypoint-scoped WGSL code and extra module-scope WGSL code.
 */
export function runStatementTest(
  t: GPUTest,
  fmt: string,
  values: TypedArrayBufferView,
  wgsl_main: string,
  extras: { global_decl?: string } = {}
) {
  const wgsl = `
struct Outputs {
  data  : array<${fmt}>,
};
var<private> count: u32 = 0;

@group(0) @binding(1) var<storage, read_write> outputs : Outputs;

fn push_output(value : ${fmt}) {
  outputs.data[count] = value;
  count += 1;
}

${extras.global_decl ?? ''}

@compute @workgroup_size(1)
fn main() {
  _ = &outputs;
  ${wgsl_main}
}
`;

  const pipeline = t.device.createComputePipeline({
    layout: 'auto',
    compute: {
      module: t.device.createShaderModule({ code: wgsl }),
      entryPoint: 'main',
    },
  });

  const maxOutputValues = 1000;
  const outputBuffer = t.createBufferTracked({
    size: 4 * (1 + maxOutputValues),
    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC,
  });

  const bindGroup = t.device.createBindGroup({
    layout: pipeline.getBindGroupLayout(0),
    entries: [{ binding: 1, resource: { buffer: outputBuffer } }],
  });

  // Run the shader.
  const encoder = t.device.createCommandEncoder();
  const pass = encoder.beginComputePass();
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, bindGroup);
  pass.dispatchWorkgroups(1);
  pass.end();
  t.queue.submit([encoder.finish()]);

  t.expectGPUBufferValuesEqual(outputBuffer, values);
}

g.test('scalar_i32_increment')
  .desc('Tests increment of scalar i32 values')
  .fn(t => {
    runStatementTest(
      t,
      'i32',
      new Int32Array([-9, 11, kValue.i32.negative.min + 1, kValue.i32.positive.max, 1]),
      `
    var a: i32 = -10;
    var b: i32 = 10;
    var c: i32 = ${kValue.i32.negative.min};
    var d: i32 = ${kValue.i32.positive.max - 1};
    var e: i32 = 0;

    a++;
    b++;
    c++;
    d++;
    e++;

    push_output(a);
    push_output(b);
    push_output(c);
    push_output(d);
    push_output(e);
`
    );
  });

g.test('scalar_i32_increment_overflow')
  .desc('Tests increment of scalar i32 values which overflows')
  .fn(t => {
    runStatementTest(
      t,
      'i32',
      new Int32Array([kValue.i32.negative.min]),
      `
    var a: i32 = ${kValue.i32.positive.max};
    a++;
    push_output(a);
`
    );
  });

g.test('scalar_u32_increment')
  .desc('Tests increment of scalar u32 values')
  .fn(t => {
    runStatementTest(
      t,
      'u32',
      new Uint32Array([1, 11, kValue.u32.max]),
      `
    var a: u32 = 0;
    var b: u32 = 10;
    var c: u32 = ${kValue.u32.max - 1};

    a++;
    b++;
    c++;

    push_output(a);
    push_output(b);
    push_output(c);
`
    );
  });

g.test('scalar_u32_increment_overflow')
  .desc('Tests increment of scalar u32 values which overflows')
  .fn(t => {
    runStatementTest(
      t,
      'u32',
      new Uint32Array([0]),
      `
    var a: u32 = ${kValue.u32.max};
    a++;
    push_output(a);
`
    );
  });

g.test('scalar_i32_decrement')
  .desc('Tests decrement of scalar i32 values')
  .fn(t => {
    runStatementTest(
      t,
      'i32',
      new Int32Array([-11, 9, kValue.i32.negative.min, kValue.i32.positive.max - 1, -1]),
      `
    var a: i32 = -10;
    var b: i32 = 10;
    var c: i32 = ${kValue.i32.negative.min + 1};
    var d: i32 = ${kValue.i32.positive.max};
    var e: i32 = 0;

    a--;
    b--;
    c--;
    d--;
    e--;

    push_output(a);
    push_output(b);
    push_output(c);
    push_output(d);
    push_output(e);
`
    );
  });

g.test('scalar_i32_decrement_underflow')
  .desc('Tests decrement of scalar i32 values which underflow')
  .fn(t => {
    runStatementTest(
      t,
      'i32',
      new Int32Array([kValue.i32.positive.max]),
      `
    var a: i32 = ${kValue.i32.negative.min};
    a--;
    push_output(a);
`
    );
  });

g.test('scalar_u32_decrement')
  .desc('Tests decrement of scalar u32 values')
  .fn(t => {
    runStatementTest(
      t,
      'u32',
      new Uint32Array([0, 9, kValue.u32.max - 1]),
      `
    var a: u32 = 1;
    var b: u32 = 10;
    var c: u32 = ${kValue.u32.max};

    a--;
    b--;
    c--;

    push_output(a);
    push_output(b);
    push_output(c);
`
    );
  });

g.test('scalar_u32_decrement_underflow')
  .desc('Tests decrement of scalar u32 values which underflow')
  .fn(t => {
    runStatementTest(
      t,
      'u32',
      new Uint32Array([kValue.u32.max]),
      `
    var a: u32 = 0;
    a--;
    push_output(a);
`
    );
  });

g.test('vec2_element_increment')
  .desc('Tests increment of ve2 values')
  .fn(t => {
    runStatementTest(
      t,
      'i32',
      new Int32Array([-9, 11]),
      `
    var a = vec2(-10, 10);

    a.x++;
    a.g++;

    push_output(a.x);
    push_output(a.y);
`
    );
  });

g.test('vec3_element_increment')
  .desc('Tests increment of vec3 values')
  .fn(t => {
    runStatementTest(
      t,
      'i32',
      new Int32Array([-9, 11, kValue.i32.negative.min + 1]),
      `
    var a = vec3(-10, 10, ${kValue.i32.negative.min});

    a.x++;
    a.g++;
    a.z++;

    push_output(a.x);
    push_output(a.y);
    push_output(a.z);
`
    );
  });

g.test('vec4_element_increment')
  .desc('Tests increment of vec4 values')
  .fn(t => {
    runStatementTest(
      t,
      'i32',
      new Int32Array([-9, 11, kValue.i32.negative.min + 1, kValue.i32.positive.max]),
      `
    var a: vec4<i32> = vec4(-10, 10, ${kValue.i32.negative.min}, ${kValue.i32.positive.max - 1});

    a.x++;
    a.g++;
    a.z++;
    a.a++;

    push_output(a.x);
    push_output(a.y);
    push_output(a.z);
    push_output(a.w);
`
    );
  });

g.test('vec2_element_decrement')
  .desc('Tests decrement of vec2 values')
  .fn(t => {
    runStatementTest(
      t,
      'i32',
      new Int32Array([-11, 9]),
      `
    var a = vec2(-10, 10);

    a.x--;
    a.g--;

    push_output(a.x);
    push_output(a.y);
`
    );
  });

g.test('vec3_element_decrement')
  .desc('Tests decrement of vec3 values')
  .fn(t => {
    runStatementTest(
      t,
      'i32',
      new Int32Array([-11, 9, kValue.i32.negative.min]),
      `
    var a = vec3(-10, 10, ${kValue.i32.negative.min + 1});

    a.x--;
    a.g--;
    a.z--;

    push_output(a.x);
    push_output(a.y);
    push_output(a.z);
`
    );
  });

g.test('vec4_element_decrement')
  .desc('Tests decrement of vec4 values')
  .fn(t => {
    runStatementTest(
      t,
      'i32',
      new Int32Array([-11, 9, kValue.i32.negative.min, kValue.i32.positive.max - 1]),
      `
    var a: vec4<i32> = vec4(-10, 10, ${kValue.i32.negative.min + 1}, ${kValue.i32.positive.max});

    a.x--;
    a.g--;
    a.z--;
    a.a--;

    push_output(a.x);
    push_output(a.y);
    push_output(a.z);
    push_output(a.w);
`
    );
  });

g.test('frexp_exp_increment')
  .desc('Tests increment can be used on a frexp field')
  .fn(t => {
    runStatementTest(
      t,
      'i32',
      new Int32Array([2]),
      `
    var a = frexp(1.23);
    a.exp++;
    push_output(a.exp);
`
    );
  });

g.test('single_eval_increment')
  .desc('Tests the left-hand-side reference of an increment is computed only once.')
  .fn(t => {
    runStatementTest(
      t,
      'i32',
      new Int32Array([999, 0, 1, 2, 11, 21, 31]),
      `
    var a: array<i32,3> = array(10, 20, 30);

    push_output(999);
    a[bump()]++;
    a[bump()]++;
    a[bump()]++;
    push_output(a[0]);
    push_output(a[1]);
    push_output(a[2]);
`,
      {
        global_decl: `
  var<private> index_counter: i32 = 0;
  fn bump() -> i32 {
    let result = index_counter;
    push_output(result);
    index_counter = index_counter + 1;
    return result;
  }
  `,
      }
    );
  });

g.test('single_eval_decrement')
  .desc('Tests the left-hand-side reference of a decrement is computed only once.')
  .fn(t => {
    runStatementTest(
      t,
      'i32',
      new Int32Array([999, 0, 1, 2, 9, 19, 29]),
      `
    var a: array<i32,3> = array(10, 20, 30);

    push_output(999);
    a[bump()]--;
    a[bump()]--;
    a[bump()]--;
    push_output(a[0]);
    push_output(a[1]);
    push_output(a[2]);
`,
      {
        global_decl: `
  var<private> index_counter: i32 = 0;
  fn bump() -> i32 {
    let result = index_counter;
    push_output(result);
    index_counter = index_counter + 1;
    return result;
  }
  `,
      }
    );
  });
