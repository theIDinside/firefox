export const description = `
This test dedicatedly tests validation of GPUPrimitiveState of createRenderPipeline.
`;

import { makeTestGroup } from '../../../../common/framework/test_group.js';
import { kPrimitiveTopology, kIndexFormat } from '../../../capability_info.js';
import * as vtu from '../validation_test_utils.js';

import { CreateRenderPipelineValidationTest } from './common.js';

export const g = makeTestGroup(CreateRenderPipelineValidationTest);

g.test('strip_index_format')
  .desc(
    `If primitive.topology is not "line-strip" or "triangle-strip", primitive.stripIndexFormat must be undefined.`
  )
  .params(u =>
    u
      .combine('isAsync', [false, true])
      .combine('topology', [undefined, ...kPrimitiveTopology] as const)
      .combine('stripIndexFormat', [undefined, ...kIndexFormat] as const)
  )
  .fn(t => {
    const { isAsync, topology, stripIndexFormat } = t.params;

    const descriptor = t.getDescriptor({ primitive: { topology, stripIndexFormat } });

    const _success =
      topology === 'line-strip' || topology === 'triangle-strip' || stripIndexFormat === undefined;
    vtu.doCreateRenderPipelineTest(t, isAsync, _success, descriptor);
  });

g.test('unclipped_depth')
  .desc(`If primitive.unclippedDepth is true, features must contain "depth-clip-control".`)
  .params(u => u.combine('isAsync', [false, true]).combine('unclippedDepth', [false, true]))
  .fn(t => {
    const { isAsync, unclippedDepth } = t.params;

    const descriptor = t.getDescriptor({ primitive: { unclippedDepth } });

    const _success = !unclippedDepth || t.device.features.has('depth-clip-control');
    vtu.doCreateRenderPipelineTest(t, isAsync, _success, descriptor);
  });
